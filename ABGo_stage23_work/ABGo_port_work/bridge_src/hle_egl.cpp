// EGL HLE for the ARM32 guest. EGL/native handles are represented by 32-bit guest tokens;
// raw 64-bit host pointers are never exposed to guest code.
#include <EGL/egl.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "common.h"
#include "hle.h"
#include "jni_bridge.h"

namespace {
std::mutex g_mu;
std::atomic<uint32_t> g_swap_trace_count{0};
uint32_t g_next = 0x70000000u;
// Android's EGL headers typedef EGLDisplay, EGLConfig, EGLContext, and
// EGLSurface to the same native pointer type (void*). C++ therefore cannot
// specialize templates separately for those aliases. Keep one host-handle
// registry for EGL's opaque pointer handles and a separate one for windows.
template<class T> std::unordered_map<uint32_t,T>& table();
template<> std::unordered_map<uint32_t,void*>& table<void*>() { static std::unordered_map<uint32_t,void*> t; return t; }
template<> std::unordered_map<uint32_t,ANativeWindow*>& table<ANativeWindow*>() { static std::unordered_map<uint32_t,ANativeWindow*> t; return t; }

template<class T> std::unordered_map<uint32_t,uint32_t>& owners();
template<> std::unordered_map<uint32_t,uint32_t>& owners<void*>() { static std::unordered_map<uint32_t,uint32_t> t; return t; }

// All guest-visible opaque handles share one token namespace. Keep allocation
// collision-safe even after the 32-bit counter wraps; stale tokens must never
// silently alias a live display, config, context, surface, or native window.
bool token_in_use(uint32_t id) {
    return table<EGLDisplay>().count(id) || table<EGLConfig>().count(id) ||
           table<EGLContext>().count(id) || table<EGLSurface>().count(id) ||
           table<ANativeWindow*>().count(id);
}
uint32_t allocate_token_locked() {
    constexpr uint32_t first = 0x70000000u;
    const uint32_t initial = g_next;
    do {
        const uint32_t candidate = g_next;
        g_next += 4;
        if (g_next < first) g_next = first;
        if (!token_in_use(candidate)) return candidate;
    } while (g_next != initial);
    return 0; // Exhausted the token namespace; callers must fail safely.
}
template<class T> void set_owner(uint32_t id, uint32_t display) {
    if (!id) return;
    std::lock_guard<std::mutex> l(g_mu);
    owners<T>()[id] = display;
}
template<class T> void erase_owned_by(uint32_t display) {
    auto& own = owners<T>();
    auto& handles = table<T>();
    for (auto it = own.begin(); it != own.end();) {
        if (it->second == display) {
            handles.erase(it->first);
            it = own.erase(it);
        } else ++it;
    }
}
template<class T> uint32_t put(T v) {
    if (v == T{}) return 0;
    std::lock_guard<std::mutex> l(g_mu);
    auto& t = table<T>();
    for (auto& [id, value] : t) if (value == v) return id;
    uint32_t id = allocate_token_locked();
    if (!id) return 0;
    t.emplace(id, v); return id;
}
template<class T> T get(uint32_t id) {
    if (!id) return T{};
    std::lock_guard<std::mutex> l(g_mu);
    auto& t = table<T>(); auto i = t.find(id); return i == t.end() ? T{} : i->second;
}
template<class T> bool token_belongs_to(uint32_t id, uint32_t display) {
    if (!id) return true; // EGL_NO_CONTEXT/EGL_NO_SURFACE are valid null handles.
    std::lock_guard<std::mutex> l(g_mu);
    auto& handles = table<T>();
    auto h = handles.find(id);
    if (h == handles.end()) return false;
    auto& own = owners<T>();
    auto o = own.find(id);
    return o != own.end() && o->second == display;
}
template<class T> T get_owned(uint32_t id, uint32_t display) {
    if (!id) return T{};
    std::lock_guard<std::mutex> l(g_mu);
    auto& handles = table<T>();
    auto h = handles.find(id);
    if (h == handles.end()) return T{};
    auto& own = owners<T>();
    auto o = own.find(id);
    return o != own.end() && o->second == display ? h->second : T{};
}
template<class T> uint32_t put_owned(T v, uint32_t display) {
    if (v == T{} || !display) return 0;
    std::lock_guard<std::mutex> l(g_mu);
    auto& handles = table<T>();
    auto& own = owners<T>();
    // A native EGL handle may be returned through more than one display. Reuse
    // a token only when both the handle and its owning display match.
    for (const auto& entry : handles) {
        auto owner = own.find(entry.first);
        if (entry.second == v && owner != own.end() && owner->second == display)
            return entry.first;
    }
    uint32_t id = allocate_token_locked();
    if (!id) return 0;
    handles.emplace(id, v);
    own[id] = display;
    return id;
}
template<class T> void drop(uint32_t id) {
    std::lock_guard<std::mutex> l(g_mu);
    table<T>().erase(id);
    if constexpr (!std::is_same_v<T, EGLDisplay> && !std::is_same_v<T, ANativeWindow*>)
        owners<T>().erase(id);
}
uint32_t g_eglGetDisplay(uint32_t native) {
    uint32_t token = put(eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(static_cast<uintptr_t>(native))));
    LOGI("EGL getDisplay native=0x%x token=0x%x error=0x%x", native, token, eglGetError());
    return token;
}
uint32_t g_eglInitialize(uint32_t d, GPtr<EGLint> major, GPtr<EGLint> minor) {
    EGLBoolean ok = eglInitialize(get<EGLDisplay>(d), major.h(), minor.h());
    LOGI("EGL initialize display=0x%x ok=%u version=%d.%d error=0x%x", d, ok, major ? *major.h() : -1, minor ? *minor.h() : -1, eglGetError());
    return ok;
}
uint32_t g_eglTerminate(uint32_t d) {
    EGLDisplay display = get<EGLDisplay>(d);
    if (display == EGL_NO_DISPLAY) return EGL_FALSE;
    EGLBoolean result = eglTerminate(display);
    if (result) {
        // EGLTerminate invalidates resources belonging to this display. The guest
        // only receives opaque tokens, so remove stale mappings rather than allowing
        // a later accidental token reuse to refer to invalid host EGL handles.
        std::lock_guard<std::mutex> l(g_mu);
        table<EGLDisplay>().erase(d);
        erase_owned_by<EGLConfig>(d);
        erase_owned_by<EGLContext>(d);
        erase_owned_by<EGLSurface>(d);
    }
    return result;
}
uint32_t g_eglBindAPI(uint32_t api) { return eglBindAPI(api); }
uint32_t g_eglChooseConfig(uint32_t d, GPtr<const EGLint> attrs, GPtr<uint32_t> configs, int32_t size, GPtr<EGLint> count) {
    if (size < 0 || size > 4096) return EGL_FALSE;
    std::vector<EGLConfig> tmp(static_cast<size_t>(size)); EGLint n = 0;
    EGLBoolean ok = eglChooseConfig(get<EGLDisplay>(d), attrs.h(), tmp.data(), size, &n);
    if (count) *count.h() = n;
    if (configs && ok) for (int i=0; i<n && i<size; ++i) { uint32_t token = put_owned(tmp[static_cast<size_t>(i)], d); configs.h()[i] = token; }
    return ok;
}
uint32_t g_eglGetConfigs(uint32_t d, GPtr<uint32_t> configs, int32_t size, GPtr<EGLint> count) {
    if (size < 0 || size > 4096) return EGL_FALSE;
    std::vector<EGLConfig> tmp(static_cast<size_t>(size)); EGLint n = 0;
    EGLBoolean ok = eglGetConfigs(get<EGLDisplay>(d), configs ? tmp.data() : nullptr, size, &n);
    if (count) *count.h() = n;
    if (configs && ok) for (int i=0; i<n && i<size; ++i) { uint32_t token = put_owned(tmp[static_cast<size_t>(i)], d); configs.h()[i] = token; }
    return ok;
}
uint32_t g_eglGetConfigAttrib(uint32_t d,uint32_t c,uint32_t a,GPtr<EGLint> v) {
    if (!token_belongs_to<EGLConfig>(c,d) || !c) return EGL_FALSE;
    return eglGetConfigAttrib(get<EGLDisplay>(d),get_owned<EGLConfig>(c,d),a,v.h());
}
uint32_t g_eglCreateContext(uint32_t d,uint32_t c,uint32_t share,GPtr<const EGLint> attrs) {
    if (!c || !token_belongs_to<EGLConfig>(c,d) || !token_belongs_to<EGLContext>(share,d)) return 0;
    return put_owned(eglCreateContext(get<EGLDisplay>(d),get_owned<EGLConfig>(c,d),get_owned<EGLContext>(share,d),attrs.h()), d);
}
uint32_t g_eglDestroyContext(uint32_t d,uint32_t c) {
    if (!c || !token_belongs_to<EGLContext>(c,d)) return EGL_FALSE;
    EGLBoolean r=eglDestroyContext(get<EGLDisplay>(d),get_owned<EGLContext>(c,d)); if(r) drop<EGLContext>(c); return r;
}
uint32_t g_eglCreateWindowSurface(uint32_t d,uint32_t c,uint32_t w,GPtr<const EGLint> attrs) {
    if (!c || !token_belongs_to<EGLConfig>(c,d)) return 0;
    ANativeWindow* win=get<ANativeWindow*>(w); if(!win) return 0;
    EGLSurface surface = eglCreateWindowSurface(get<EGLDisplay>(d), get_owned<EGLConfig>(c,d), win, attrs.h());
    EGLint width = 0, height = 0;
    if (surface != EGL_NO_SURFACE) { eglQuerySurface(get<EGLDisplay>(d), surface, EGL_WIDTH, &width); eglQuerySurface(get<EGLDisplay>(d), surface, EGL_HEIGHT, &height); }
    uint32_t token = put_owned(surface, d);
    LOGI("EGL createWindowSurface display=0x%x config=0x%x window=0x%x token=0x%x size=%dx%d error=0x%x", d, c, w, token, width, height, eglGetError());
    return token;
}
uint32_t g_eglDestroySurface(uint32_t d,uint32_t s) {
    if (!s || !token_belongs_to<EGLSurface>(s,d)) return EGL_FALSE;
    EGLBoolean r=eglDestroySurface(get<EGLDisplay>(d),get_owned<EGLSurface>(s,d)); if(r) drop<EGLSurface>(s); return r;
}
uint32_t g_eglMakeCurrent(uint32_t d,uint32_t draw,uint32_t read,uint32_t ctx) {
    if (!token_belongs_to<EGLSurface>(draw,d) || !token_belongs_to<EGLSurface>(read,d) || !token_belongs_to<EGLContext>(ctx,d)) return EGL_FALSE;
    EGLBoolean ok = eglMakeCurrent(get<EGLDisplay>(d), get_owned<EGLSurface>(draw,d), get_owned<EGLSurface>(read,d), get_owned<EGLContext>(ctx,d));
    LOGI("EGL makeCurrent display=0x%x draw=0x%x read=0x%x context=0x%x ok=%u error=0x%x", d, draw, read, ctx, ok, eglGetError());
    return ok;
}
uint32_t g_eglSwapBuffers(uint32_t d,uint32_t s) {
    if (!s || !token_belongs_to<EGLSurface>(s,d)) return EGL_FALSE;
    EGLBoolean ok = eglSwapBuffers(get<EGLDisplay>(d), get_owned<EGLSurface>(s,d));
    uint32_t n = g_swap_trace_count.fetch_add(1, std::memory_order_relaxed);
    if (n < 8 || !ok) LOGI("EGL swapBuffers #%u display=0x%x surface=0x%x ok=%u error=0x%x", n + 1, d, s, ok, eglGetError());
    return ok;
}
uint32_t g_eglSwapInterval(uint32_t d,int32_t i) { return eglSwapInterval(get<EGLDisplay>(d),i); }
uint32_t g_eglSurfaceAttrib(uint32_t d,uint32_t s,int32_t a,int32_t v) {
    if (!s || !token_belongs_to<EGLSurface>(s,d)) return EGL_FALSE;
    return eglSurfaceAttrib(get<EGLDisplay>(d),get_owned<EGLSurface>(s,d),a,v);
}
uint32_t g_eglQuerySurface(uint32_t d,uint32_t s,int32_t a,GPtr<EGLint> v) {
    if (!s || !token_belongs_to<EGLSurface>(s,d)) return EGL_FALSE;
    return eglQuerySurface(get<EGLDisplay>(d),get_owned<EGLSurface>(s,d),a,v.h());
}
uint32_t g_eglQueryString(uint32_t d,uint32_t n) { const char* s=eglQueryString(get<EGLDisplay>(d),n); return s ? arena::gstrdup(s) : 0; }
uint32_t g_eglGetCurrentContext() {
    EGLContext context = eglGetCurrentContext();
    EGLDisplay current = eglGetCurrentDisplay();
    return current != EGL_NO_DISPLAY ? put_owned(context, put(current)) : put(context);
}
uint32_t g_eglGetCurrentSurface(uint32_t which) {
    EGLSurface surface = eglGetCurrentSurface(which);
    EGLDisplay current = eglGetCurrentDisplay();
    return current != EGL_NO_DISPLAY ? put_owned(surface, put(current)) : put(surface);
}
uint32_t g_eglGetProcAddress(GStr name) { if(!name) return 0; return hle::resolve(name.h()); }
uint32_t g_eglGetError() { return eglGetError(); }
uint32_t g_ANativeWindow_fromSurface(uint32_t, uint32_t surface) {
    ANativeWindow* w = ANativeWindow_fromSurface(jni::env(), jni::obj(surface));
    if (!w) return 0;
    // fromSurface returns an acquired reference: each guest result needs its own token so
    // a matching guest release balances exactly one acquired reference.
    std::lock_guard<std::mutex> l(g_mu);
    uint32_t id = allocate_token_locked();
    if (!id) { ANativeWindow_release(w); return 0; }
    table<ANativeWindow*>().emplace(id, w);
    return id;
}
void g_ANativeWindow_release(uint32_t token) { ANativeWindow* w=get<ANativeWindow*>(token); if(w) ANativeWindow_release(w); drop<ANativeWindow*>(token); }
int32_t g_ANativeWindow_setBuffersGeometry(uint32_t token,int32_t w,int32_t h,int32_t f) { ANativeWindow* win=get<ANativeWindow*>(token); return win ? ANativeWindow_setBuffersGeometry(win,w,h,f) : -1; }
const hle::Def kDefs[] = {
 H("eglGetDisplay",g_eglGetDisplay), H("eglInitialize",g_eglInitialize), H("eglTerminate",g_eglTerminate), H("eglBindAPI",g_eglBindAPI),
 H("eglChooseConfig",g_eglChooseConfig), H("eglGetConfigs",g_eglGetConfigs), H("eglGetConfigAttrib",g_eglGetConfigAttrib),
 H("eglCreateContext",g_eglCreateContext), H("eglDestroyContext",g_eglDestroyContext), H("eglCreateWindowSurface",g_eglCreateWindowSurface),
 H("eglDestroySurface",g_eglDestroySurface), H("eglMakeCurrent",g_eglMakeCurrent), H("eglSwapBuffers",g_eglSwapBuffers),
 H("eglSwapInterval",g_eglSwapInterval), H("eglSurfaceAttrib",g_eglSurfaceAttrib), H("eglQuerySurface",g_eglQuerySurface),
 H("eglQueryString",g_eglQueryString), H("eglGetCurrentContext",g_eglGetCurrentContext), H("eglGetCurrentSurface",g_eglGetCurrentSurface),
 H("eglGetProcAddress",g_eglGetProcAddress), H("eglGetError",g_eglGetError), H("ANativeWindow_fromSurface",g_ANativeWindow_fromSurface),
 H("ANativeWindow_release",g_ANativeWindow_release), H("ANativeWindow_setBuffersGeometry",g_ANativeWindow_setBuffersGeometry),
};
}
void hle::register_egl() { add_defs(kDefs, std::size(kDefs)); }
