// libandroid asset imports. Guest AAssetManager/AAsset values are guest cells mapped to host objects.
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "hle.h"
#include "jni_bridge.h"

namespace {

struct Asset {
    AAsset* host = nullptr;
    uint32_t buffer = 0;  // guest copy for AAsset_getBuffer
};
struct AssetDir {
    AAssetDir* host = nullptr;
    uint32_t filename = 0;  // guest-owned filename valid until the next call/close
};

std::mutex g_mu;
std::unordered_map<uint32_t, AAssetManager*> g_managers;
std::unordered_map<uint32_t, Asset> g_assets;
std::unordered_map<uint32_t, AssetDir> g_dirs;

AAssetManager* manager(uint32_t token) {
    std::lock_guard l(g_mu);
    auto it = g_managers.find(token);
    return it == g_managers.end() ? nullptr : it->second;
}

uint32_t g_AAssetManager_fromJava(uint32_t, uint32_t jmgr) {
    AAssetManager* m = AAssetManager_fromJava(jni::env(), jni::obj(jmgr));
    if (!m) return 0;
    std::lock_guard l(g_mu);
    for (auto& [token, host] : g_managers)
        if (host == m) return token;
    uint32_t token = arena::static_alloc(4, 4);
    g_managers[token] = m;
    return token;
}

uint32_t g_AAssetManager_open(uint32_t mgr, GStr name, int32_t mode) {
    AAssetManager* m = manager(mgr);
    if (!m) return 0;
    AAsset* a = AAssetManager_open(m, name.h(), mode);
    if (!a) return 0;
    uint32_t token = arena::gcalloc(1, 16);
    std::lock_guard l(g_mu);
    g_assets[token].host = a;
    return token;
}

void g_AAsset_close(uint32_t t) {
    Asset copy;
    {
        std::lock_guard l(g_mu);
        auto it = g_assets.find(t);
        if (it == g_assets.end()) return;
        copy = it->second;
        g_assets.erase(it);
    }
    AAsset_close(copy.host);
    if (copy.buffer) arena::gfree(copy.buffer);
    arena::gfree(t);
}

uint32_t g_AAsset_getBuffer(uint32_t t) {
    std::lock_guard l(g_mu);
    auto it = g_assets.find(t);
    if (it == g_assets.end()) return 0;
    Asset& a = it->second;
    if (!a.buffer) {
        const void* p = AAsset_getBuffer(a.host);
        if (!p) return 0;
        auto len64 = AAsset_getLength64(a.host);
        if (len64 < 0 || static_cast<uint64_t>(len64) > UINT32_MAX) return 0;
        uint32_t len = static_cast<uint32_t>(len64);
        a.buffer = arena::gmalloc(len ? len : 1);
        if (!a.buffer) return 0;
        memcpy(arena::h(a.buffer), p, len);
    }
    return a.buffer;
}

int64_t g_AAsset_getLength64(uint32_t t) {
    std::lock_guard l(g_mu);
    auto it = g_assets.find(t);
    return it == g_assets.end() ? 0 : AAsset_getLength64(it->second.host);
}
int32_t g_AAsset_getLength(uint32_t t) { return static_cast<int32_t>(g_AAsset_getLength64(t)); }
int64_t g_AAsset_getRemainingLength64(uint32_t t) {
    std::lock_guard l(g_mu);
    auto it = g_assets.find(t);
    return it == g_assets.end() ? 0 : AAsset_getRemainingLength64(it->second.host);
}
int32_t g_AAsset_read(uint32_t t, GPtr<void> buf, uint32_t n) {
    std::lock_guard l(g_mu);
    auto it = g_assets.find(t);
    return it == g_assets.end() ? -1 : AAsset_read(it->second.host, buf.h(), n);
}
int32_t g_AAsset_seek(uint32_t t, int32_t off, int32_t whence) {
    std::lock_guard l(g_mu);
    auto it = g_assets.find(t);
    return it == g_assets.end() ? -1 : static_cast<int32_t>(AAsset_seek(it->second.host, off, whence));
}

uint32_t g_AAssetManager_openDir(uint32_t mgr, GStr dirName) {
    AAssetManager* m = manager(mgr);
    if (!m) return 0;
    AAssetDir* d = AAssetManager_openDir(m, dirName.h());
    if (!d) return 0;
    uint32_t token = arena::gcalloc(1, 8);
    if (!token) { AAssetDir_close(d); return 0; }
    std::lock_guard l(g_mu);
    g_dirs[token].host = d;
    return token;
}

uint32_t g_AAssetDir_getNextFileName(uint32_t t) {
    std::lock_guard l(g_mu);
    auto it = g_dirs.find(t);
    if (it == g_dirs.end()) return 0;
    const char* name = AAssetDir_getNextFileName(it->second.host);
    if (it->second.filename) { arena::gfree(it->second.filename); it->second.filename = 0; }
    if (!name) return 0;
    it->second.filename = arena::gstrdup(name);
    return it->second.filename;
}

void g_AAssetDir_rewind(uint32_t t) {
    std::lock_guard l(g_mu);
    auto it = g_dirs.find(t);
    if (it != g_dirs.end()) {
        AAssetDir_rewind(it->second.host);
        if (it->second.filename) { arena::gfree(it->second.filename); it->second.filename = 0; }
    }
}

void g_AAssetDir_close(uint32_t t) {
    AssetDir copy;
    {
        std::lock_guard l(g_mu);
        auto it = g_dirs.find(t);
        if (it == g_dirs.end()) return;
        copy = it->second;
        g_dirs.erase(it);
    }
    AAssetDir_close(copy.host);
    if (copy.filename) arena::gfree(copy.filename);
    arena::gfree(t);
}

int32_t g_AAsset_openFileDescriptor(uint32_t t, GPtr<int64_t> outStart, GPtr<int64_t> outLength) {
    std::lock_guard l(g_mu);
    auto it = g_assets.find(t);
    if (it == g_assets.end()) return -1;
    off_t start = 0, length = 0;
    int fd = AAsset_openFileDescriptor(it->second.host, &start, &length);
    if (fd >= 0) {
        if (outStart) *outStart.h() = static_cast<int64_t>(start);
        if (outLength) *outLength.h() = static_cast<int64_t>(length);
    }
    return fd;
}

const hle::Def kDefs[] = {
    H("AAssetManager_fromJava", g_AAssetManager_fromJava), H("AAssetManager_open", g_AAssetManager_open),
    H("AAsset_close", g_AAsset_close), H("AAsset_getBuffer", g_AAsset_getBuffer),
    H("AAsset_getLength64", g_AAsset_getLength64), H("AAsset_getLength", g_AAsset_getLength),
    H("AAsset_getRemainingLength64", g_AAsset_getRemainingLength64), H("AAsset_read", g_AAsset_read),
    H("AAsset_seek", g_AAsset_seek), H("AAssetManager_openDir", g_AAssetManager_openDir),
    H("AAssetDir_getNextFileName", g_AAssetDir_getNextFileName), H("AAssetDir_rewind", g_AAssetDir_rewind),
    H("AAssetDir_close", g_AAssetDir_close), H("AAsset_openFileDescriptor", g_AAsset_openFileDescriptor),
};

}  // namespace

void hle::register_android() { add_defs(kDefs, std::size(kDefs)); }
