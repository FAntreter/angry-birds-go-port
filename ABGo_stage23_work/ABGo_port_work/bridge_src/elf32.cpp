#include "elf32.h"

#include <elf.h>

#include <algorithm>
#include <memory>
#include <mutex>

#include "arena.h"
#include "common.h"
#include "cpu.h"
#include "hle.h"

namespace elf {
namespace {

constexpr uint32_t kPtArmExidx = 0x70000001;
constexpr uint32_t R_ARM_NONE_ = 0, R_ARM_ABS32_ = 2, R_ARM_REL32_ = 3, R_ARM_GLOB_DAT_ = 21,
                   R_ARM_JUMP_SLOT_ = 22, R_ARM_RELATIVE_ = 23;

std::vector<std::unique_ptr<Module>> g_modules;
std::mutex g_mu;

template <typename T>
const T* at(const uint8_t* data, size_t size, size_t off, size_t count = 1) {
    if (off > size || count * sizeof(T) > size - off) fatal("ELF read out of bounds");
    return reinterpret_cast<const T*>(data + off);
}

uint32_t resolve_import(const std::string& name, bool weak, const std::string& importer, uint32_t relocation) {
    if (uint32_t a = find_export(name)) return a;
    if (uint32_t a = hle::resolve(name)) return a;
    if (weak) {
        bridge_log("UNRESOLVED WEAK IMPORT: module=%s symbol=%s relocation=%u (resolved as null)",
                   importer.c_str(), name.c_str(), relocation);
        return 0;
    }
    // Preserve the exact loader context in the app-side crash log. This is emitted
    // during relocation, before guest constructors can call the diagnostic thunk.
    bridge_log("UNRESOLVED STRONG IMPORT: module=%s symbol=%s relocation=%u (diagnostic thunk installed)",
               importer.c_str(), name.c_str(), relocation);
    return hle::unresolved_thunk(name);
}

}  // namespace

// Read DT_NEEDED entries from an on-disk ARM32 ELF before mapping it into guest memory.
// Every offset and string is bounds-checked so malformed APK libraries fail cleanly.
std::vector<std::string> needed(const uint8_t* data, size_t size) {
    auto* eh = at<Elf32_Ehdr>(data, size, 0);
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32 ||
        eh->e_machine != EM_ARM || eh->e_type != ET_DYN)
        fatal("dependency scan: not an ARM32 shared object");
    auto* ph = at<Elf32_Phdr>(data, size, eh->e_phoff, eh->e_phnum);
    const Elf32_Phdr* dynamic = nullptr;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == PT_DYNAMIC) { dynamic = &ph[i]; break; }
    }
    if (!dynamic) fatal("dependency scan: no PT_DYNAMIC");
    at<uint8_t>(data, size, dynamic->p_offset, dynamic->p_filesz);
    auto* dyn = at<Elf32_Dyn>(data, size, dynamic->p_offset, dynamic->p_filesz / sizeof(Elf32_Dyn));
    uint32_t strtab_vaddr = 0, strsz = 0;
    std::vector<uint32_t> offsets;
    const size_t dyn_count = dynamic->p_filesz / sizeof(Elf32_Dyn);
    bool terminated = false;
    for (size_t i = 0; i < dyn_count; ++i) {
        if (dyn[i].d_tag == DT_NULL) { terminated = true; break; }
        if (dyn[i].d_tag == DT_STRTAB) strtab_vaddr = dyn[i].d_un.d_ptr;
        else if (dyn[i].d_tag == DT_STRSZ) strsz = dyn[i].d_un.d_val;
        else if (dyn[i].d_tag == DT_NEEDED) offsets.push_back(dyn[i].d_un.d_val);
    }
    if (!terminated) fatal("dependency scan: unterminated dynamic table");
    if (offsets.empty()) return {};
    if (!strtab_vaddr || !strsz) fatal("dependency scan: missing DT_STRTAB/DT_STRSZ");

    // Convert the string table's virtual address to a file offset using a PT_LOAD segment.
    size_t strtab_off = size;
    for (int i = 0; i < eh->e_phnum; ++i) {
        const auto& p = ph[i];
        if (p.p_type != PT_LOAD || strtab_vaddr < p.p_vaddr) continue;
        uint64_t delta = uint64_t(strtab_vaddr) - p.p_vaddr;
        if (delta <= p.p_filesz && strsz <= p.p_filesz - delta) {
            strtab_off = size_t(p.p_offset + delta);
            break;
        }
    }
    if (strtab_off == size) fatal("dependency scan: string table outside file-backed PT_LOAD");
    at<uint8_t>(data, size, strtab_off, strsz);

    std::vector<std::string> result;
    result.reserve(offsets.size());
    for (uint32_t off : offsets) {
        if (off >= strsz) fatal("dependency scan: DT_NEEDED string offset out of range");
        const char* start = reinterpret_cast<const char*>(data + strtab_off + off);
        const void* end = memchr(start, '\0', strsz - off);
        if (!end) fatal("dependency scan: unterminated DT_NEEDED string");
        result.emplace_back(start, static_cast<const char*>(end) - start);
    }
    return result;
}

Module* load(const std::string& name, const uint8_t* data, size_t size) {
    auto* eh = at<Elf32_Ehdr>(data, size, 0);
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32 ||
        eh->e_machine != EM_ARM || eh->e_type != ET_DYN)
        fatal("%s: not an ARM32 shared object", name.c_str());
    auto* ph = at<Elf32_Phdr>(data, size, eh->e_phoff, eh->e_phnum);

    uint32_t lo = UINT32_MAX, hi = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        lo = std::min(lo, ph[i].p_vaddr & ~0xFFFu);
        hi = std::max(hi, (ph[i].p_vaddr + ph[i].p_memsz + 0xFFF) & ~0xFFFu);
    }
    auto mod = std::make_unique<Module>();
    Module* m = mod.get();
    m->name = name;
    m->size = hi - lo;
    m->base = arena::module_alloc(m->size);
    m->bias = m->base - lo;

    uint32_t dyn = 0;
    for (int i = 0; i < eh->e_phnum; ++i) {
        const auto& p = ph[i];
        if (p.p_type == PT_LOAD) {
            at<uint8_t>(data, size, p.p_offset, p.p_filesz);
            memcpy(arena::h(p.p_vaddr + m->bias), data + p.p_offset, p.p_filesz);
            if (p.p_memsz > p.p_filesz)
                memset(arena::h(p.p_vaddr + m->bias + p.p_filesz), 0, p.p_memsz - p.p_filesz);
        } else if (p.p_type == PT_DYNAMIC) {
            dyn = p.p_vaddr + m->bias;
        } else if (p.p_type == kPtArmExidx) {
            m->exidx = p.p_vaddr + m->bias;
            m->exidx_count = p.p_memsz / 8;
        }
    }
    if (!dyn) fatal("%s: no PT_DYNAMIC", name.c_str());

    uint32_t symtab = 0, strtab = 0, nsyms = 0, rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0;
    uint32_t init_array = 0, init_arraysz = 0;
    for (auto* d = arena::hp<Elf32_Dyn>(dyn); d->d_tag != DT_NULL; ++d) {
        uint32_t v = d->d_un.d_val;
        switch (d->d_tag) {
        case DT_SYMTAB: symtab = v + m->bias; break;
        case DT_STRTAB: strtab = v + m->bias; break;
        case DT_HASH: nsyms = arena::hp<uint32_t>(v + m->bias)[1]; break;
        case DT_REL: rel = v + m->bias; break;
        case DT_RELSZ: relsz = v; break;
        case DT_JMPREL: jmprel = v + m->bias; break;
        case DT_PLTRELSZ: pltrelsz = v; break;
        case DT_INIT: m->init = v + m->bias; break;
        case DT_INIT_ARRAY: init_array = v + m->bias; break;
        case DT_INIT_ARRAYSZ: init_arraysz = v; break;
        case DT_RELA:
        case DT_RELR: fatal("%s: unsupported relocation table (tag %d)", name.c_str(), int(d->d_tag));
        default: break;
        }
    }
    if (!nsyms) {  // no DT_HASH: fall back to the section headers
        auto* sh = at<Elf32_Shdr>(data, size, eh->e_shoff, eh->e_shnum);
        for (int i = 0; i < eh->e_shnum; ++i)
            if (sh[i].sh_type == SHT_DYNSYM) nsyms = sh[i].sh_size / sizeof(Elf32_Sym);
    }
    auto* syms = arena::hp<Elf32_Sym>(symtab);
    auto symname = [&](uint32_t i) { return std::string(arena::hp<const char>(strtab + syms[i].st_name)); };

    for (uint32_t i = 1; i < nsyms; ++i) {
        const auto& s = syms[i];
        if (s.st_shndx == SHN_UNDEF) continue;
        int bind = ELF32_ST_BIND(s.st_info), type = ELF32_ST_TYPE(s.st_info);
        if (type == STT_FUNC) m->funcs.emplace_back(s.st_value + m->bias, symname(i));
        if (bind == STB_GLOBAL || bind == STB_WEAK) m->exports[symname(i)] = s.st_value + m->bias;
    }
    std::sort(m->funcs.begin(), m->funcs.end());

    {
        std::lock_guard l(g_mu);
        g_modules.push_back(std::move(mod));
    }

    std::unordered_map<uint32_t, uint32_t> cache;
    auto sym_value = [&](uint32_t idx, uint32_t relocation) -> uint32_t {
        if (auto it = cache.find(idx); it != cache.end()) return it->second;
        const auto& s = syms[idx];
        uint32_t v = s.st_shndx != SHN_UNDEF ? s.st_value + m->bias
                                             : resolve_import(symname(idx), ELF32_ST_BIND(s.st_info) == STB_WEAK, m->name, relocation);
        cache[idx] = v;
        return v;
    };
    auto apply = [&](uint32_t table, uint32_t bytes) {
        for (uint32_t off = 0; off < bytes; off += sizeof(Elf32_Rel)) {
            auto* r = arena::hp<Elf32_Rel>(table + off);
            uint32_t type = ELF32_R_TYPE(r->r_info), sym = ELF32_R_SYM(r->r_info);
            uint32_t where = r->r_offset + m->bias;
            auto* p = arena::hp<uint32_t>(where);
            switch (type) {
            case R_ARM_NONE_: break;
            case R_ARM_RELATIVE_: *p += m->bias; break;
            case R_ARM_ABS32_: *p += sym_value(sym, type); break;
            case R_ARM_REL32_: *p += sym_value(sym, type) - where; break;
            case R_ARM_GLOB_DAT_:
            case R_ARM_JUMP_SLOT_: *p = sym_value(sym, type); break;
            default: fatal("%s: unsupported relocation type %u", name.c_str(), type);
            }
        }
    };
    if (rel) apply(rel, relsz);
    if (jmprel) apply(jmprel, pltrelsz);

    for (uint32_t i = 0; i < init_arraysz / 4; ++i) {
        uint32_t f = arena::hp<uint32_t>(init_array)[i];
        if (f != 0 && f != 0xFFFFFFFFu) m->init_array.push_back(f);
    }
    LOGI("loaded %s at 0x%08x size 0x%x, %zu exports, exidx %u", name.c_str(), m->base, m->size, m->exports.size(),
         m->exidx_count);
    return m;
}

void run_init(Module* m) {
    size_t index = 0;
    if (m->init) {
        bridge_log("CTOR: %s DT_INIT begin at 0x%08x", m->name.c_str(), m->init);
        cpu::call(m->init, {});
        bridge_log("CTOR: %s DT_INIT returned", m->name.c_str());
    }
    for (uint32_t f : m->init_array) {
        bridge_log("CTOR: %s init_array[%zu] begin at 0x%08x", m->name.c_str(), index, f);
        const bool trace_crypto_startup = (m->name == "libABK.so" && index == 676);
        if (trace_crypto_startup) {
            bridge_log("CRYPTO STARTUP: tracing NSS/FIPS initialization calls");
            cpu::watch_abgo_crypto_startup(m->bias);
        }
        cpu::call(f, {});
        if (trace_crypto_startup) cpu::clear_abgo_crypto_startup_watch();
        bridge_log("CTOR: %s init_array[%zu] returned", m->name.c_str(), index);
        ++index;
    }
    LOGI("%s: ran %zu constructors", m->name.c_str(), m->init_array.size() + (m->init ? 1 : 0));
}

uint32_t find_export(const std::string& name) {
    std::lock_guard l(g_mu);
    for (auto& m : g_modules)
        if (auto it = m->exports.find(name); it != m->exports.end()) return it->second;
    return 0;
}

const Module* owner(uint32_t addr) {
    std::lock_guard l(g_mu);
    for (auto& m : g_modules)
        if (addr >= m->base && addr < m->base + m->size) return m.get();
    return nullptr;
}

bool owns(uint32_t addr) { return owner(addr) != nullptr; }

bool lookup_symbol(uint32_t addr, uint32_t* symbol_addr, std::string* symbol_name) {
    const Module* m = owner(addr);
    if (!m || m->funcs.empty()) return false;
    const uint32_t query = addr & ~1u;
    auto it = std::upper_bound(m->funcs.begin(), m->funcs.end(),
                               std::make_pair(query, std::string("\xff")));
    if (it == m->funcs.begin()) return false;
    --it;
    if (symbol_addr) *symbol_addr = it->first;
    if (symbol_name) *symbol_name = it->second;
    return true;
}

std::string describe(uint32_t addr) {
    char buf[256];
    if (const Module* m = owner(addr)) {
        uint32_t a = addr & ~1u;
        auto it = std::upper_bound(m->funcs.begin(), m->funcs.end(), std::make_pair(a, std::string("\xff")));
        if (it != m->funcs.begin()) {
            --it;
            snprintf(buf, sizeof buf, "0x%08x %s+0x%x (%s+0x%x)", addr, m->name.c_str(), addr - m->bias,
                     it->second.c_str(), a - (it->first & ~1u));
        } else {
            snprintf(buf, sizeof buf, "0x%08x %s+0x%x", addr, m->name.c_str(), addr - m->bias);
        }
        return buf;
    }
    std::string thunk = hle::thunk_name(addr);
    snprintf(buf, sizeof buf, "0x%08x%s%s", addr, thunk.empty() ? "" : " thunk:", thunk.c_str());
    return buf;
}

uint32_t find_exidx(uint32_t pc, int32_t* count) {
    const Module* m = owner(pc);
    if (!m || !m->exidx) { *count = 0; return 0; }
    *count = static_cast<int32_t>(m->exidx_count);
    return m->exidx;
}

}  // namespace elf
