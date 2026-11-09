#include "hle.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <mutex>
#include <signal.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdio>
#include <cerrno>

#include "elf32.h"

namespace {
// Best effort: on Android, /proc/self/cmdline argv[0] is usually the package name.
std::string package_name() {
    char buf[256] = {};
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (f) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        for (size_t i = 0; i < n; ++i)
            if (buf[i] == '\0') { buf[i] = 0; break; }
    }
    std::string p = buf;
    return p.empty() ? std::string("com.rovio.angrybirdsgo15") : p;
}

// Recursive mkdir without spawning a shell (may be restricted on some builds).
bool mkdir_p(const std::string& path) {
    size_t pos = 0;
    std::string acc;
    if (!path.empty() && path[0] == '/') acc = "/";
    while ((pos = path.find('/', pos)) != std::string::npos) {
        acc += path.substr(acc.size(), pos - acc.size() + 1);
        ++pos;
        if (mkdir(acc.c_str(), 0770) != 0 && errno != EEXIST) return false;
    }
    if (mkdir(path.c_str(), 0770) != 0 && errno != EEXIST) return false;
    return true;
}

// Permission-free locations first: the app's own external dir is writable without
// storage permission on modern Android and is readable from a normal file manager;
// app-internal dirs are the guaranteed fallback; Documents and Downloads are
// user-visible fallbacks (may require storage permission on newer Android).
std::vector<std::string> crash_log_paths() {
    std::string pkg = package_name();
    return {
        "/storage/emulated/0/Android/media/com.multiaccounts.cloneapps/fqtu/storage/emulated/0/Android/media/com.rovio.angrybirdsgo15/abgo_bridge_crash.txt",
        "/sdcard/Android/data/" + pkg + "/files/abgo_bridge_crash.txt",
        "/storage/emulated/0/Android/data/" + pkg + "/files/abgo_bridge_crash.txt",
        "/data/data/" + pkg + "/files/abgo_bridge_crash.txt",
        "/data/user/0/" + pkg + "/files/abgo_bridge_crash.txt",
        "/sdcard/Documents/abgo_bridge_crash.txt",
        "/storage/emulated/0/Documents/abgo_bridge_crash.txt",
        "/sdcard/Download/abgo_bridge_crash.txt",
        "/storage/emulated/0/Download/abgo_bridge_crash.txt",
    };
}

void append_crash_line(const char* line) {
    for (const std::string& path : crash_log_paths()) {
        size_t slash = path.rfind('/');
        if (slash != std::string::npos)
            mkdir_p(path.substr(0, slash));
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
        if (fd < 0) continue;
        size_t n = strlen(line);
        const char* p = line;
        while (n) { ssize_t w = write(fd, p, n); if (w <= 0) break; p += w; n -= static_cast<size_t>(w); }
        write(fd, "\n", 1); fsync(fd); close(fd);
    }
}
}
namespace {
void write_signal_number(int fd, int sig) {
    char digits[16]; size_t n = 0; unsigned int v = static_cast<unsigned int>(sig);
    do { digits[n++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v && n < sizeof digits);
    while (n) { char c = digits[--n]; (void)write(fd, &c, 1); }
}
void crash_signal_handler(int sig, siginfo_t*, void*) {
    static const char prefix[] = "FATAL SIGNAL: ";
    const char* path = crash_log_paths().front().c_str();
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    if (fd >= 0) {
        (void)write(fd, prefix, sizeof(prefix) - 1);
        write_signal_number(fd, sig);
        (void)write(fd, "\n", 1);
        (void)close(fd);
    }
    struct sigaction action{}; action.sa_handler = SIG_DFL; sigemptyset(&action.sa_mask);
    (void)sigaction(sig, &action, nullptr);
    (void)kill(getpid(), sig);
    _exit(128 + sig);
}
}
void install_crash_handlers() {
    append_crash_line("=== Angry Birds Go bridge session ===");
    struct sigaction action{}; action.sa_sigaction = crash_signal_handler;
    sigemptyset(&action.sa_mask); action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    const int signals[] = { SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV };
    for (int sig : signals) {
        if (sigaction(sig, &action, nullptr) != 0)
            bridge_log("WARNING: could not install crash handler for signal %d", sig);
    }
    bridge_log("native crash handlers installed (SIGABRT/SIGBUS/SIGFPE/SIGILL/SIGSEGV)");
}
void bridge_log(const char* fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, ABSW_TAG, "%s", buf); append_crash_line(buf);
}
void fatal(const char* fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_FATAL, ABSW_TAG, "FATAL: %s", buf);
    char line[1100]; snprintf(line, sizeof line, "FATAL: %s", buf); append_crash_line(line); abort();
}

namespace hle {
namespace {

struct Entry {
    std::string name;
    Fn fn;
};

constexpr uint32_t kMaxThunks = arena::kThunkSize / 8;
std::mutex g_mu;
std::vector<Entry> g_entries;  // index = svc immediate; guarded by g_mu
std::array<std::atomic<Fn>, kMaxThunks> g_fns{};  // lock-free dispatch view of g_entries
std::unordered_map<std::string, uint32_t> g_by_name;  // name -> guest address (thunk or data)

void unresolved_called(cpu::Ctx& c) {
    uint32_t pc = c.r(15);
    fatal("unresolved import called: %s (from %s)", thunk_name(pc - 4).c_str(), elf::describe(c.r(14)).c_str());
}

}  // namespace

uint32_t add(const std::string& name, Fn fn) {
    std::lock_guard l(g_mu);
    if (auto it = g_by_name.find(name); it != g_by_name.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(g_entries.size());
    if (idx >= kMaxThunks) fatal("thunk table full");
    g_entries.push_back({name, fn});
    g_fns[idx].store(fn, std::memory_order_release);
    uint32_t addr = arena::kThunkBase + idx * 8;
    auto* code = arena::hp<uint32_t>(addr);
    code[0] = 0xEF000000u | idx;  // svc #idx
    code[1] = 0xE12FFF1Eu;        // bx lr
    g_by_name[name] = addr;
    return addr;
}

void add_defs(const Def* defs, size_t n) {
    for (size_t i = 0; i < n; ++i) add(defs[i].name, defs[i].fn);
}

void add_data(const std::string& name, uint32_t addr) {
    std::lock_guard l(g_mu);
    g_by_name[name] = addr;
}

uint32_t resolve(const std::string& name) {
    std::lock_guard l(g_mu);
    auto it = g_by_name.find(name);
    return it == g_by_name.end() ? 0 : it->second;
}

uint32_t unresolved_thunk(const std::string& name) { return add("<unresolved>" + name, &unresolved_called); }

std::string thunk_name(uint32_t addr) {
    if (addr < arena::kThunkBase || addr >= arena::kThunkBase + arena::kThunkSize) return {};
    std::lock_guard l(g_mu);
    uint32_t idx = (addr - arena::kThunkBase) / 8;
    return idx < g_entries.size() ? g_entries[idx].name : std::string();
}

void dispatch(uint32_t idx, cpu::Ctx& c) {
    Fn fn = idx < kMaxThunks ? g_fns[idx].load(std::memory_order_acquire) : nullptr;
    if (!fn) fatal("bad svc #%u at %s", idx, elf::describe(c.r(15)).c_str());
#if ABSW_TRACE
    LOGI("call %s from %s", thunk_name(arena::kThunkBase + idx * 8).c_str(), elf::describe(c.r(14)).c_str());
#endif
    fn(c);
}

void init() {
    register_libc();
    const uint32_t getgid_addr = resolve("getgid");
    bridge_log("HLE libc registration check: getgid=%s (0x%08x)",
               getgid_addr ? "REGISTERED" : "MISSING", getgid_addr);
    if (!getgid_addr) fatal("HLE initialization failed to register libc getgid");
    register_stdio();
    register_pthread();
    register_gles();
    register_android();
    register_egl();
    register_opensles();
}

}  // namespace hle
