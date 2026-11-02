// Entry point: the arm64/x86_64 libAngryBirdsStarWars.so that hosts the original ARM32 engine.
#include <dlfcn.h>
#include <jni.h>

#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "arena.h"
#include "common.h"
#include "cpu.h"
#include "elf32.h"
#include "hle.h"
#include "jni_bridge.h"

namespace {

const uint8_t kGuestRt[] = {
#include "guest_rt_blob.inc"
};

// Go's ARM32 game image is stored beside the host bridge with an explicit suffix so it
// cannot be mistaken for a same-named ARM64 Android library.
constexpr const char* kGuestLibFile = "libABK_arm32.so";

// Dependencies that are supplied by the host Android process or the HLE layer rather
// than by a guest ARM32 ELF. Keep this list explicit: silently skipping arbitrary missing
// DT_NEEDED entries would hide packaging mistakes.
const std::unordered_set<std::string> kHostLibraries = {
    "libandroid.so", "libEGL.so", "libGLESv2.so", "libjnigraphics.so",
    "liblog.so", "libOpenSLES.so", "libstdc++.so", "libm.so", "libc.so", "libdl.so"
};

std::string guest_filename(const std::string& soname) {
    // Guest dependencies use a suffix because the bridge package may also contain
    // host-ABI libraries with the standard Android soname.
    if (soname == "libgnustl_shared.so" || soname == "libSkynestSDK.so" ||
        soname == "libadcolony.so" || soname == "libjs.so") {
        return soname.substr(0, soname.size() - 3) + "_arm32.so";
    }
    return soname;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return data;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data.resize(static_cast<size_t>(n));
    if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
    fclose(f);
    return data;
}

std::string own_dir() {
    Dl_info info{};
    dladdr(reinterpret_cast<void*>(&read_file), &info);
    std::string p = info.dli_fname ? info.dli_fname : "";
    auto slash = p.rfind('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

// Recursively load DT_NEEDED guest libraries before the importer so cross-library
// relocations can see dependency exports. Constructors run dependency-first.
// This deliberately fails on dependency cycles: the current relocator is single-pass
// and does not yet support two-phase mapping/relocation for cyclic ELF graphs.
elf::Module* load_guest_tree(const std::string& soname, const std::string& filename,
                             std::unordered_map<std::string, elf::Module*>& loaded,
                             std::unordered_set<std::string>& active) {
    if (auto it = loaded.find(soname); it != loaded.end()) return it->second;
    if (!active.insert(soname).second)
        fatal("cyclic guest DT_NEEDED dependency at %s (two-phase loader required)", soname.c_str());

    std::vector<uint8_t> image = read_file(own_dir() + "/" + filename);
    if (image.empty()) fatal("cannot read guest dependency %s (expected %s)", soname.c_str(), filename.c_str());

    for (const std::string& dep : elf::needed(image.data(), image.size())) {
        if (kHostLibraries.count(dep)) continue;
        const std::string dep_file = guest_filename(dep);
        load_guest_tree(dep, dep_file, loaded, active);
    }

    elf::Module* module = elf::load(soname, image.data(), image.size());
    loaded.emplace(soname, module);
    active.erase(soname);
    elf::run_init(module);
    return module;
}

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
#if defined(__aarch64__)
    LOGI("absw ARM32 bridge starting (host arm64)");
#else
    LOGI("absw ARM32 bridge starting (host x86_64)");
#endif
    arena::init();
    cpu::init();
    hle::init();
    jni::init(vm, env);

    elf::Module* rt = elf::load("libabsw_rt.so", kGuestRt, sizeof kGuestRt);
    elf::run_init(rt);

    std::unordered_map<std::string, elf::Module*> loaded;
    std::unordered_set<std::string> active;
    elf::Module* game = load_guest_tree("libABK.so", kGuestLibFile, loaded, active);

    // Android calls JNI_OnLoad for a library loaded by Java. The guest SDK is
    // brought in as a DT_NEEDED dependency instead, so invoke its lifecycle hook
    // explicitly after all guest dependencies and ELF constructors are ready.
    // The Go SDK's JNI_OnLoad stores the JavaVM and returns JNI_VERSION_1_6.
    if (auto it = loaded.find("libSkynestSDK.so"); it != loaded.end()) {
        auto on_load = it->second->exports.find("JNI_OnLoad");
        if (on_load == it->second->exports.end())
            fatal("libSkynestSDK.so is missing JNI_OnLoad");
        jint sdk_version;
        {
            jni::Scope scope(env);
            sdk_version = static_cast<jint>(cpu::call(on_load->second, {jni::guest_vm(), 0}));
        }
        LOGI("libSkynestSDK.so JNI_OnLoad returned 0x%x", sdk_version);
        if (sdk_version != JNI_VERSION_1_6)
            fatal("libSkynestSDK.so requested unsupported JNI version 0x%x", sdk_version);
    } else {
        fatal("libSkynestSDK.so was not loaded as a guest dependency");
    }

    int bound = jni::bind_exports(env);
    LOGI("bound %d native methods", bound);

    jint version = JNI_VERSION_1_6;
    if (auto it = game->exports.find("JNI_OnLoad"); it != game->exports.end()) {
        jni::Scope scope(env);
        version = static_cast<jint>(cpu::call(it->second, {jni::guest_vm(), 0}));
        LOGI("guest JNI_OnLoad returned 0x%x", version);
    }
    return version;
}
