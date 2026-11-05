#include "cpu.h"

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cinttypes>
#include <cstdarg>
#include <memory>
#include <mutex>

#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <dynarmic/interface/A32/coprocessor.h>
#include <dynarmic/interface/exclusive_monitor.h>

#include "arena.h"
#include "common.h"
#include "elf32.h"
#include "hle.h"

namespace cpu {
namespace {

using Dynarmic::A32::CoprocReg;
using Dynarmic::A32::Jit;
using Dynarmic::A32::VAddr;

constexpr size_t kMaxCpus = 128;

constexpr uint32_t kStackSize = 2u << 20;

static std::array<uint32_t, 10> g_crypto_watch_addresses{};
static std::atomic<uint32_t> g_crypto_watch_mask{0};
static std::atomic<bool> g_crypto_watch_active{false};
static std::atomic<uint32_t> g_crypto_code_logs{0};
static constexpr const char* kCryptoWatchNames[] = {
    "enter SECOID_Init", "enter BL_Init", "enter RNG_RNGInit",
    "enter sftk_fips_RSA_PowerUpSelfTest", "enter BLAPI_SHVerify",
    "return SECOID_Init", "return BL_Init", "return RNG_RNGInit",
    "return sftk_fips_RSA_PowerUpSelfTest", "return BLAPI_SHVerify"
};

Dynarmic::ExclusiveMonitor* g_monitor;
std::mutex g_pid_mu;
std::bitset<kMaxCpus> g_pid_used;
uint32_t g_return_pc;  // guest address of "svc #kSvcReturn"

size_t alloc_pid() {
    std::lock_guard l(g_pid_mu);
    for (size_t i = 0; i < kMaxCpus; ++i)
        if (!g_pid_used[i]) { g_pid_used[i] = true; return i; }
    fatal("too many guest CPUs");
}
void free_pid(size_t p) {
    std::lock_guard l(g_pid_mu);
    g_pid_used[p] = false;
}

struct GuestCpu;

// CP15 c13 (TPIDRURO) read-only thread register used by bionic's __get_tls().
class Cp15 final : public Dynarmic::A32::Coprocessor {
public:
    uint32_t tpidruro = 0;
    std::optional<Callback> CompileInternalOperation(bool, unsigned, CoprocReg, CoprocReg, CoprocReg, unsigned) override { return std::nullopt; }
    CallbackOrAccessOneWord CompileSendOneWord(bool, unsigned, CoprocReg, CoprocReg, unsigned) override { return std::monostate{}; }
    CallbackOrAccessTwoWords CompileSendTwoWords(bool, unsigned, CoprocReg) override { return std::monostate{}; }
    CallbackOrAccessOneWord CompileGetOneWord(bool two, unsigned opc1, CoprocReg crn, CoprocReg crm, unsigned opc2) override {
        if (!two && opc1 == 0 && crn == CoprocReg::C13 && crm == CoprocReg::C0 && opc2 == 3) return &tpidruro;
        return std::monostate{};
    }
    CallbackOrAccessTwoWords CompileGetTwoWords(bool, unsigned, CoprocReg) override { return std::monostate{}; }
    std::optional<Callback> CompileLoadWords(bool, bool, CoprocReg, std::optional<uint8_t>) override { return std::nullopt; }
    std::optional<Callback> CompileStoreWords(bool, bool, CoprocReg, std::optional<uint8_t>) override { return std::nullopt; }
};

struct Callbacks final : Dynarmic::A32::UserCallbacks {
    GuestCpu* owner = nullptr;

    template <typename T>
    static T rd(VAddr a) { T v; memcpy(&v, arena::base + a, sizeof v); return v; }
    template <typename T>
    static void wr(VAddr a, T v) { memcpy(arena::base + a, &v, sizeof v); }
    template <typename T>
    static bool cas(VAddr a, T v, T expected) {
        return __atomic_compare_exchange_n(reinterpret_cast<T*>(arena::base + a), &expected, v, false,
                                           __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }

    void LogCryptoTranslateState(VAddr a, uint32_t rng);
    std::optional<uint32_t> MemoryReadCode(VAddr a) override {
        if (g_crypto_watch_active.load(std::memory_order_relaxed)) {
            const uint32_t rng = g_crypto_watch_addresses[2];
            if (rng && a >= rng && a < rng + 0x4000u) {
                const uint32_t n = g_crypto_code_logs.fetch_add(1, std::memory_order_relaxed);
                if (n < 160) {
                    bridge_log("CRYPTO CODE: translated guest PC=0x%08x offset=0x%x", a, a - rng);
                    if (a >= rng + 0x14f0u && a < rng + 0x1580u)
                        LogCryptoTranslateState(a, rng);
                }
            }
            for (uint32_t i = 0; i < g_crypto_watch_addresses.size(); ++i) {
                if (a == g_crypto_watch_addresses[i]) {
                    const uint32_t bit = 1u << i;
                    if ((g_crypto_watch_mask.fetch_or(bit) & bit) == 0)
                        bridge_log("CRYPTO STARTUP: entered %s at 0x%08x", kCryptoWatchNames[i], a);
                    break;
                }
            }
        }
        return rd<uint32_t>(a);
    }
    uint8_t MemoryRead8(VAddr a) override { return rd<uint8_t>(a); }
    uint16_t MemoryRead16(VAddr a) override { return rd<uint16_t>(a); }
    uint32_t MemoryRead32(VAddr a) override { return rd<uint32_t>(a); }
    uint64_t MemoryRead64(VAddr a) override { return rd<uint64_t>(a); }
    void MemoryWrite8(VAddr a, uint8_t v) override { wr(a, v); }
    void MemoryWrite16(VAddr a, uint16_t v) override { wr(a, v); }
    void MemoryWrite32(VAddr a, uint32_t v) override { wr(a, v); }
    void MemoryWrite64(VAddr a, uint64_t v) override { wr(a, v); }
    bool MemoryWriteExclusive8(VAddr a, uint8_t v, uint8_t e) override { return cas(a, v, e); }
    bool MemoryWriteExclusive16(VAddr a, uint16_t v, uint16_t e) override { return cas(a, v, e); }
    bool MemoryWriteExclusive32(VAddr a, uint32_t v, uint32_t e) override { return cas(a, v, e); }
    bool MemoryWriteExclusive64(VAddr a, uint64_t v, uint64_t e) override { return cas(a, v, e); }

    void InterpreterFallback(VAddr pc, size_t n) override;
    void CallSVC(uint32_t swi) override;
    void ExceptionRaised(VAddr pc, Dynarmic::A32::Exception e) override;
    void AddTicks(uint64_t ticks) override;
    uint64_t GetTicksRemaining() override { return ~uint64_t{0} >> 2; }
};

struct GuestCpu {
    Callbacks cb;
    std::shared_ptr<Cp15> cp15 = std::make_shared<Cp15>();
    std::unique_ptr<Jit> jit;
    size_t pid;
    bool returned = false;

    GuestCpu(uint32_t tls, size_t cache) : pid(alloc_pid()) {
        cb.owner = this;
        cp15->tpidruro = tls;
        Dynarmic::A32::UserConfig cfg;
        cfg.callbacks = &cb;
        cfg.processor_id = pid;
        cfg.global_monitor = g_monitor;
        cfg.arch_version = Dynarmic::A32::ArchVersion::v8;
        cfg.fastmem_pointer = reinterpret_cast<uintptr_t>(arena::base);
        cfg.recompile_on_fastmem_failure = true;
        cfg.coprocessors[15] = cp15;
        cfg.enable_cycle_counting = true;
        cfg.define_unpredictable_behaviour = true;
        cfg.code_cache_size = cache;
        jit = std::make_unique<Jit>(cfg);
    }
    ~GuestCpu() {
        jit.reset();
        g_monitor->ClearProcessor(pid);
        free_pid(pid);
    }
};

void Callbacks::AddTicks(uint64_t ticks) {
    if (!g_crypto_watch_active.load(std::memory_order_relaxed) || !owner || !owner->jit)
        return;
    const uint32_t pc = owner->jit->Regs()[15];
    const uint32_t rng = g_crypto_watch_addresses[2];
    static std::atomic<uint32_t> all_tick_logs{0};
    const uint32_t tick_n = all_tick_logs.fetch_add(1, std::memory_order_relaxed);
    if (tick_n < 160)
        bridge_log("CRYPTO TICK: post_block_pc=0x%08x cpsr=0x%08x ticks=%" PRIu64,
                   pc, owner->jit->Cpsr(), ticks);
    if (rng && pc >= rng && pc < rng + 0x4000u) {
        static std::atomic<uint32_t> execution_logs{0};
        const uint32_t n = execution_logs.fetch_add(1, std::memory_order_relaxed);
        if (n < 120)
            bridge_log("CRYPTO EXEC: guest PC=0x%08x offset_from_RNG=0x%x ticks=%" PRIu64,
                       pc, pc - rng, ticks);
        // Capture the live register state around the rng_init block seen in
        // device traces (rng_init + 0x230..0x260). AddTicks runs after a
        // translated block, so this records the actual post-block state.
        if (pc >= rng + 0x14f0u && pc < rng + 0x1580u) {
            static std::atomic<uint32_t> rng_state_logs{0};
            const uint32_t m = rng_state_logs.fetch_add(1, std::memory_order_relaxed);
            if (m < 48) {
                const auto& r = owner->jit->Regs();
                bridge_log("CRYPTO STATE: pc=0x%08x off=0x%x cpsr=0x%08x ticks=%" PRIu64,
                           r[15], r[15] - rng, owner->jit->Cpsr(), ticks);
                bridge_log("CRYPTO STATE: r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x",
                           r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
                bridge_log("CRYPTO STATE: r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x sp=%08x lr=%08x",
                           r[8], r[9], r[10], r[11], r[12], r[13], r[14]);
            }
        }
    }
}

void Callbacks::LogCryptoTranslateState(VAddr a, uint32_t rng) {
    if (!owner || !owner->jit)
        return;
    const auto& r = owner->jit->Regs();
    bridge_log("CRYPTO TRANSLATE STATE: entry_pc=0x%08x code_pc=0x%08x cpsr=0x%08x",
               r[15], a, owner->jit->Cpsr());
    bridge_log("CRYPTO TRANSLATE STATE: r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x",
               r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    bridge_log("CRYPTO TRANSLATE STATE: r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x sp=%08x lr=%08x",
               r[8], r[9], r[10], r[11], r[12], r[13], r[14]);
}

struct ThreadState {
    std::vector<std::unique_ptr<GuestCpu>> cpus;  // index = host->guest nesting depth
    size_t depth = 0;
    uint32_t stack_top = 0;
    uint32_t tls = 0;
    ~ThreadState() {
        cpus.clear();
        if (stack_top) arena::stack_free(stack_top);
        if (tls) arena::gfree(tls);
    }
};

pthread_key_t g_state_key;
thread_local ThreadState* t_state;

ThreadState& state() {
    if (!t_state) {
        t_state = new ThreadState();
        t_state->stack_top = arena::stack_alloc(kStackSize);
        t_state->tls = arena::gcalloc(64, 4);
        auto* slots = arena::hp<uint32_t>(t_state->tls);
        slots[0] = t_state->tls;            // TLS_SLOT_SELF
        slots[1] = static_cast<uint32_t>(gettid());  // TLS_SLOT_THREAD_ID
        pthread_setspecific(g_state_key, t_state);
    }
    return *t_state;
}

void Callbacks::CallSVC(uint32_t swi) {
    if (swi == kSvcReturn) {
        owner->returned = true;
        owner->jit->HaltExecution();
        return;
    }
    Ctx ctx{owner->jit.get()};
    hle::dispatch(swi, ctx);
}

void Callbacks::InterpreterFallback(VAddr pc, size_t n) {
    LOGE("interpreter fallback requested at %s (n=%zu)", elf::describe(pc).c_str(), n);
    dump_and_die(owner->jit.get(), "unsupported instruction");
}

void Callbacks::ExceptionRaised(VAddr pc, Dynarmic::A32::Exception e) {
    using E = Dynarmic::A32::Exception;
    switch (e) {
    case E::Yield:
    case E::SendEvent:
    case E::SendEventLocal:
    case E::WaitForEvent:
    case E::WaitForInterrupt:
    case E::PreloadData:
    case E::PreloadDataWithIntentToWrite:
    case E::PreloadInstruction:
        owner->jit->Regs()[15] = pc + ((owner->jit->Cpsr() & 0x20) ? 2 : 4);
        return;
    default:
        LOGE("guest exception %d at %s", static_cast<int>(e), elf::describe(pc).c_str());
        dump_and_die(owner->jit.get(), "guest exception");
    }
}

}  // namespace

// Fault logger chained in front of dynarmic's fastmem handler (installed after the first JIT exists).
static struct sigaction g_prev_segv, g_prev_bus;
static std::atomic<int> g_fault_logs{0};

static void fault_logger(int sig, siginfo_t* info, void* ctx) {
    if (g_fault_logs.fetch_add(1) < 8) {
        auto addr = reinterpret_cast<uintptr_t>(info->si_addr);
        auto base = reinterpret_cast<uintptr_t>(arena::base);
        if (addr >= base && addr < base + (uintptr_t{1} << 32))
            LOGW("signal %d: guest address 0x%08x (tid %d)", sig, static_cast<uint32_t>(addr - base), gettid());
        else
            LOGW("signal %d: host address %p (tid %d)", sig, info->si_addr, gettid());
    }
    const struct sigaction& prev = sig == SIGSEGV ? g_prev_segv : g_prev_bus;
    if (prev.sa_flags & SA_SIGINFO) {
        prev.sa_sigaction(sig, info, ctx);
    } else if (prev.sa_handler == SIG_DFL || prev.sa_handler == SIG_IGN) {
        signal(sig, SIG_DFL);
        raise(sig);
    } else {
        prev.sa_handler(sig);
    }
}

static void install_fault_logger() {
    struct sigaction sa{};
    sa.sa_sigaction = fault_logger;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_prev_segv);
    sigaction(SIGBUS, &sa, &g_prev_bus);
}

uint32_t& Ctx::r(int i) { return jit->Regs()[static_cast<size_t>(i)]; }

uint32_t Ctx::arg(int n) {
    if (n < 4) return jit->Regs()[static_cast<size_t>(n)];
    uint32_t sp = jit->Regs()[13];
    uint32_t v;
    memcpy(&v, arena::base + sp + 4u * static_cast<uint32_t>(n - 4), 4);
    return v;
}

void Ctx::ret(uint32_t r0) { jit->Regs()[0] = r0; }
void Ctx::ret64(uint64_t v) {
    jit->Regs()[0] = static_cast<uint32_t>(v);
    jit->Regs()[1] = static_cast<uint32_t>(v >> 32);
}

Args& Args::f(float v) { return w(std::bit_cast<uint32_t>(v)); }
Args& Args::d64(uint64_t v) {
    if (words.size() & 1) words.push_back(0);
    words.push_back(static_cast<uint32_t>(v));
    words.push_back(static_cast<uint32_t>(v >> 32));
    return *this;
}

void watch_abgo_crypto_startup(uint32_t bias) {
    // Function entry points and the instruction immediately after each BL
    // in sftk_startup_tests (libABK.so offset 0x00c88e8).
    constexpr uint32_t offsets[] = {
        0x00a458cc, 0x00a78844, 0x00a641dc, 0x00b004f0, 0x00bfd2ec,
        0x00c8910, 0x00c891c, 0x00c8928, 0x00c8934, 0x00c8950
    };
    for (size_t i = 0; i < g_crypto_watch_addresses.size(); ++i)
        g_crypto_watch_addresses[i] = bias + offsets[i];
    g_crypto_watch_mask.store(0, std::memory_order_relaxed);
    g_crypto_code_logs.store(0, std::memory_order_relaxed);
    g_crypto_watch_active.store(true, std::memory_order_release);
}

void clear_abgo_crypto_startup_watch() {
    g_crypto_watch_active.store(false, std::memory_order_release);
}

void init() {
    g_monitor = new Dynarmic::ExclusiveMonitor(kMaxCpus);
    pthread_key_create(&g_state_key, [](void* p) {
        t_state = static_cast<ThreadState*>(p);
        delete t_state;
        t_state = nullptr;
    });
    // Kernel user helpers at their fixed ARM Linux addresses (old libgcc atomics jump there).
    static const uint8_t kKuser[] = {
#include "kuser_blob.inc"
    };
    arena::commit(0xFFFF0000u, 0x1000);
    memcpy(arena::h(0xFFFF0F60u), kKuser, sizeof kKuser);

    // ARM-mode return trampoline: svc #kSvcReturn ; b .
    g_return_pc = arena::static_alloc(8, 8);
    auto* code = arena::hp<uint32_t>(g_return_pc);
    code[0] = 0xEF000000u | kSvcReturn;
    code[1] = 0xEAFFFFFEu;
}

uint64_t call(uint32_t fn, const std::vector<uint32_t>& words) {
    ThreadState& t = state();
    const size_t d = t.depth;
    while (t.cpus.size() <= d)
        t.cpus.push_back(std::make_unique<GuestCpu>(t.tls, t.cpus.empty() ? (96u << 20) : (16u << 20)));
    static std::once_flag logger_once;  // dynarmic installs its handler with the first JIT
    std::call_once(logger_once, install_fault_logger);
    GuestCpu& c = *t.cpus[d];
    auto& R = c.jit->Regs();

    uint32_t sp = d == 0 ? t.stack_top : t.cpus[d - 1]->jit->Regs()[13] - 256;
    const size_t nstack = words.size() > 4 ? words.size() - 4 : 0;
    sp = (sp - static_cast<uint32_t>(nstack * 4)) & ~7u;
    for (size_t i = 0; i < nstack; ++i)
        memcpy(arena::base + sp + 4 * i, &words[4 + i], 4);
    for (size_t i = 0; i < 4; ++i) R[i] = i < words.size() ? words[i] : 0;
    R[13] = sp;
    R[14] = g_return_pc;
    R[15] = fn & ~1u;
    c.jit->SetCpsr(0x10u | ((fn & 1) ? 0x20u : 0u));

    c.returned = false;
    ++t.depth;
    const uint32_t watched_rng = g_crypto_watch_addresses[2];
    if (g_crypto_watch_active.load(std::memory_order_relaxed) &&
        watched_rng != 0 && (fn & ~1u) == watched_rng) {
        // Step only the RNG init function so we can observe the exact guest
        // instruction that stops progressing. Calling Step() from outside Run()
        // avoids relying on post-block callbacks for this diagnostic.
        uint32_t steps = 0;
        while (!c.returned && steps < 250000u) {
            const uint32_t before_pc = R[15];
            const uint32_t before_cpsr = c.jit->Cpsr();
            const bool thumb = (before_cpsr & 0x20u) != 0;
            uint32_t instruction = 0;
            if (thumb) {
                uint16_t halfword = 0;
                memcpy(&halfword, arena::base + (before_pc & ~1u), sizeof halfword);
                instruction = halfword;
            } else {
                memcpy(&instruction, arena::base + (before_pc & ~3u), sizeof instruction);
            }
            c.jit->Step();
            ++steps;
            if (before_pc >= watched_rng + 0x14d0u &&
                before_pc < watched_rng + 0x1560u) {
                const auto& sr = c.jit->Regs();
                bridge_log("CRYPTO STEP: n=%u pc=0x%08x insn=0x%08x next=0x%08x cpsr=0x%08x",
                           steps, before_pc, instruction, sr[15], c.jit->Cpsr());
                bridge_log("CRYPTO STEP REG: r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x",
                           sr[0], sr[1], sr[2], sr[3], sr[4], sr[5], sr[6], sr[7]);
                bridge_log("CRYPTO STEP REG: r8=%08x r9=%08x r10=%08x r11=%08x r12=%08x sp=%08x lr=%08x",
                           sr[8], sr[9], sr[10], sr[11], sr[12], sr[13], sr[14]);
            }
        }
        bridge_log("CRYPTO STEP END: steps=%u returned=%d pc=0x%08x",
                   steps, c.returned ? 1 : 0, R[15]);
    } else {
        c.jit->Run();
    }
    --t.depth;
    if (!c.returned) dump_and_die(c.jit.get(), "guest call halted without returning");
    c.jit->ClearHalt(Dynarmic::HaltReason::UserDefined1);
    return R[0] | (uint64_t{R[1]} << 32);
}

uint32_t thread_tls() { return state().tls; }
uint32_t thread_errno_addr() { return state().tls + 8; }  // TLS_SLOT_ERRNO
void set_guest_errno(int e) { *arena::hp<int32_t>(thread_errno_addr()) = e; }

uint32_t run_thread_entry(uint32_t fn, uint32_t arg) {
    return static_cast<uint32_t>(call(fn, {arg}));
}

void dump_and_die(Jit* jit, const char* why) {
    const auto& R = jit->Regs();
    LOGE("=== guest fault: %s ===", why);
    LOGE("pc=%s lr=%s", elf::describe(R[15]).c_str(), elf::describe(R[14]).c_str());
    for (int i = 0; i < 16; i += 4)
        LOGE("r%-2d=%08x r%-2d=%08x r%-2d=%08x r%-2d=%08x", i, R[i], i + 1, R[i + 1], i + 2, R[i + 2], i + 3, R[i + 3]);
    LOGE("cpsr=%08x", jit->Cpsr());
    uint32_t sp = R[13];
    for (int i = 0; i < 32; ++i) {
        uint32_t v;
        memcpy(&v, arena::base + sp + 4 * i, 4);
        if (elf::owns(v)) LOGE("  stack[%d] %s", i, elf::describe(v).c_str());
    }
    fatal("%s", why);
}

}  // namespace cpu
