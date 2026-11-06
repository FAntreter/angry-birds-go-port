// libc / libm / libdl imports of the guest, implemented on the host bionic.
#include <arpa/inet.h>
#include <android/log.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <math.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <pwd.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/auxv.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <linux/futex.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <wchar.h>
#include <wctype.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "elf32.h"
#include "hle.h"

namespace {

using arena::g;
using arena::h;
using arena::hp;

// ---------------------------------------------------------------- memory
GPtr<void> g_malloc(uint32_t n) { return {arena::gmalloc(n)}; }
GPtr<void> g_memalign(uint32_t alignment, uint32_t n) { return {arena::gmemalign(alignment, n)}; }
GPtr<void> g_calloc(uint32_t n, uint32_t s) { return {arena::gcalloc(n, s)}; }
GPtr<void> g_realloc(GPtr<void> p, uint32_t n) { return {arena::grealloc(p.a, n)}; }
void g_free(GPtr<void> p) { if (p) arena::gfree(p.a); }

std::mutex g_mmap_mu;
std::unordered_map<uint32_t, uint32_t> g_mmaps;

uint32_t g_mmap(GPtr<void> addr, uint32_t len, int32_t prot, int32_t flags, int32_t fd, int32_t off) {
    constexpr int32_t kMapFixed = 0x10, kMapAnon = 0x20;
    (void)prot;
    if (flags & kMapFixed) {
        LOGW("mmap MAP_FIXED at 0x%x unsupported", addr.a);
        cpu::set_guest_errno(EINVAL);
        return 0xFFFFFFFFu;
    }
    uint32_t a = arena::gmemalign(4096, len);
    if (!a) { cpu::set_guest_errno(ENOMEM); return 0xFFFFFFFFu; }
    memset(h(a), 0, len);
    if (!(flags & kMapAnon) && fd >= 0) {
        if (pread(fd, h(a), len, off) < 0) { arena::gfree(a); cpu::set_guest_errno(errno); return 0xFFFFFFFFu; }
    }
    std::lock_guard l(g_mmap_mu);
    g_mmaps[a] = len;
    return a;
}

int32_t g_munmap(GPtr<void> addr, uint32_t) {
    std::lock_guard l(g_mmap_mu);
    if (g_mmaps.erase(addr.a)) arena::gfree(addr.a);
    return 0;
}

// ---------------------------------------------------------------- strings
GPtr<void> g_memcpy(GPtr<void> d, GPtr<const void> s, uint32_t n) { if (n) memcpy(d.h(), s.h(), n); return d; }
GPtr<void> g_memmove(GPtr<void> d, GPtr<const void> s, uint32_t n) { if (n) memmove(d.h(), s.h(), n); return d; }
GPtr<void> g_memset(GPtr<void> d, int32_t c, uint32_t n) { if (n) memset(d.h(), c, n); return d; }
// ARM EABI helper uses (destination, byte_count, value), unlike libc memset.
void g_aeabi_memset(GPtr<void> d, uint32_t n, int32_t c) { if (n) memset(d.h(), c, n); }
int32_t g_memcmp(GPtr<const void> a, GPtr<const void> b, uint32_t n) { return n ? memcmp(a.h(), b.h(), n) : 0; }
GPtr<void> g_memchr(GPtr<const void> s, int32_t c, uint32_t n) { return {n ? g(memchr(s.h(), c, n)) : 0}; }
GPtr<void> g_memrchr(GPtr<const void> s, int32_t c, uint32_t n) { return {n ? g(memrchr(s.h(), c, n)) : 0}; }
GPtr<void> g_memmem(GPtr<const void> a, uint32_t an, GPtr<const void> b, uint32_t bn) {
    return {g(memmem(a.h(), an, b.h(), bn))};
}
GPtr<void> g_memccpy(GPtr<void> d, GPtr<const void> s, int32_t c, uint32_t n) {
    auto* dst = static_cast<unsigned char*>(d.h());
    auto* src = static_cast<const unsigned char*>(s.h());
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = src[i];
        if (src[i] == static_cast<unsigned char>(c)) return {d.a + i + 1};
    }
    return {0};
}
uint32_t g_strlen(GStr s) { return static_cast<uint32_t>(strlen(s.h())); }
uint32_t g_strnlen(GStr s, uint32_t n) { return static_cast<uint32_t>(strnlen(s.h(), n)); }
GPtr<char> g_strtok(GPtr<char> s, GStr delim) { return {g(strtok(s ? s.h() : nullptr, delim.h()))}; }
int32_t g_isspace(int32_t c) { return isspace(static_cast<unsigned char>(c)); }
int32_t g_isalnum(int32_t c) { return isalnum(static_cast<unsigned char>(c)); }
int32_t g_strcmp(GStr a, GStr b) { return strcmp(a.h(), b.h()); }
int32_t g_strncmp(GStr a, GStr b, uint32_t n) { return strncmp(a.h(), b.h(), n); }
int32_t g_strcasecmp(GStr a, GStr b) { return strcasecmp(a.h(), b.h()); }
int32_t g_strncasecmp(GStr a, GStr b, uint32_t n) { return strncasecmp(a.h(), b.h(), n); }
int32_t g_strcoll(GStr a, GStr b) { return strcoll(a.h(), b.h()); }
uint32_t g_strxfrm(GPtr<char> d, GStr s, uint32_t n) { return static_cast<uint32_t>(strxfrm(d.h(), s.h(), n)); }
GPtr<char> g_strcpy(GPtr<char> d, GStr s) { strcpy(d.h(), s.h()); return d; }
uint32_t g_strlcpy(GPtr<char> d, GStr s, uint32_t n) {
    const size_t len = strlen(s.h());
    if (n) {
        const size_t copy = std::min(len, static_cast<size_t>(n - 1));
        if (copy) memcpy(d.h(), s.h(), copy);
        d.h()[copy] = '\0';
    }
    return static_cast<uint32_t>(len);
}
GPtr<char> g_strncpy(GPtr<char> d, GStr s, uint32_t n) { strncpy(d.h(), s.h(), n); return d; }
GPtr<char> g_strcat(GPtr<char> d, GStr s) { strcat(d.h(), s.h()); return d; }
GPtr<char> g_strncat(GPtr<char> d, GStr s, uint32_t n) { strncat(d.h(), s.h(), n); return d; }
GPtr<char> g_strchr(GStr s, int32_t c) { return {g(strchr(s.h(), c))}; }
GPtr<char> g_strrchr(GStr s, int32_t c) { return {g(strrchr(s.h(), c))}; }
GPtr<char> g_strstr(GStr a, GStr b) { return {g(strstr(a.h(), b.h()))}; }
GPtr<char> g_strpbrk(GStr a, GStr b) { return {g(strpbrk(a.h(), b.h()))}; }
uint32_t g_strcspn(GStr a, GStr b) { return static_cast<uint32_t>(strcspn(a.h(), b.h())); }
GPtr<char> g_strdup(GStr s) { return {arena::gstrdup(s.h())}; }
GPtr<char> g_strtok_r(GPtr<char> s, GStr delim, GPtr<uint32_t> save) {
    char* hs = hp<char>(*save.h());
    char* r = strtok_r(s.h(), delim.h(), &hs);
    *save.h() = g(hs);
    return {g(r)};
}

std::mutex g_static_mu;
std::unordered_map<std::string, uint32_t> g_static_strings;
uint32_t interned(const std::string& s) {  // stable guest copy of a host string
    std::lock_guard l(g_static_mu);
    auto [it, fresh] = g_static_strings.try_emplace(s, 0);
    if (fresh) it->second = arena::static_strdup(s.c_str());
    return it->second;
}

GPtr<char> g_strerror(int32_t e) { return {interned(strerror(e))}; }
int32_t g_strerror_r(int32_t e, GPtr<char> buf, uint32_t n) {
    if (n) snprintf(buf.h(), n, "%s", strerror(e));
    return 0;
}
GPtr<char> g_basename(GStr path) {
    static thread_local uint32_t buf = arena::static_alloc(PATH_MAX + 1, 4);
    const char* p = path ? path.h() : "";
    std::string s(p);
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    auto slash = s.rfind('/');
    if (slash != std::string::npos && s.size() > 1) s = s.substr(slash + 1);
    if (s.empty()) s = ".";
    snprintf(hp<char>(buf), PATH_MAX + 1, "%s", s.c_str());
    return {buf};
}
int32_t g_fnmatch(GStr pat, GStr str, int32_t flags) { return fnmatch(pat.h(), str.h(), flags); }

// Guest strings live in the persistent arena, so putenv's retained pointer
// remains valid for the lifetime of the emulated process.
int32_t g_putenv(GStr assignment) {
    if (!assignment || !strchr(assignment.h(), '=')) { errno = EINVAL; return -1; }
    return putenv(const_cast<char*>(assignment.h()));
}
GPtr<char> g_mktemp(GPtr<char> pattern) {
    if (!pattern) { errno = EINVAL; return {0}; }
    char* result = mktemp(pattern.h());
    return {result ? g(result) : 0};
}

// ---------------------------------------------------------------- wide chars (wchar_t is 32-bit on both sides)
uint32_t g_wcslen(GPtr<const wchar_t> s) { return static_cast<uint32_t>(wcslen(s.h())); }
GPtr<wchar_t> g_wmemchr(GPtr<const wchar_t> s, int32_t c, uint32_t n) { return {n ? g(wmemchr(s.h(), c, n)) : 0}; }
int32_t g_wmemcmp(GPtr<const wchar_t> a, GPtr<const wchar_t> b, uint32_t n) { return n ? wmemcmp(a.h(), b.h(), n) : 0; }
GPtr<wchar_t> g_wmemcpy(GPtr<wchar_t> d, GPtr<const wchar_t> s, uint32_t n) { if (n) wmemcpy(d.h(), s.h(), n); return d; }
GPtr<wchar_t> g_wmemmove(GPtr<wchar_t> d, GPtr<const wchar_t> s, uint32_t n) { if (n) wmemmove(d.h(), s.h(), n); return d; }
GPtr<wchar_t> g_wmemset(GPtr<wchar_t> d, int32_t c, uint32_t n) { if (n) wmemset(d.h(), c, n); return d; }
int32_t g_wcscoll(GPtr<const wchar_t> a, GPtr<const wchar_t> b) { return wcscoll(a.h(), b.h()); }
uint32_t g_wcsxfrm(GPtr<wchar_t> d, GPtr<const wchar_t> s, uint32_t n) { return static_cast<uint32_t>(wcsxfrm(d.h(), s.h(), n)); }
uint32_t g_btowc(int32_t c) { return btowc(c); }
int32_t g_wctob(uint32_t c) { return wctob(c); }
uint32_t g_towlower(uint32_t c) { return towlower(c); }
uint32_t g_towupper(uint32_t c) { return towupper(c); }
uint32_t g_wctype(GStr name) { return static_cast<uint32_t>(wctype(name.h())); }
int32_t g_iswctype(uint32_t c, uint32_t t) { return iswctype(c, static_cast<wctype_t>(t)); }
uint32_t g_mbrtowc(GPtr<wchar_t> pwc, GStr s, uint32_t n, GPtr<uint32_t> ps) {
    mbstate_t st{};
    if (ps) memcpy(&st, ps.h(), 4);
    size_t r = mbrtowc(pwc.h(), s.h(), n, ps ? &st : nullptr);
    if (ps) memcpy(ps.h(), &st, 4);
    return static_cast<uint32_t>(r);
}
uint32_t g_wcrtomb(GPtr<char> s, uint32_t wc, GPtr<uint32_t> ps) {
    mbstate_t st{};
    if (ps) memcpy(&st, ps.h(), 4);
    size_t r = wcrtomb(s.h(), static_cast<wchar_t>(wc), ps ? &st : nullptr);
    if (ps) memcpy(ps.h(), &st, 4);
    return static_cast<uint32_t>(r);
}

// ---------------------------------------------------------------- conversions
int32_t g_atoi(GStr s) { return atoi(s.h()); }
int32_t g_strtol(GStr s, GPtr<uint32_t> end, int32_t base) {
    char* e;
    long v = strtol(s.h(), &e, base);
    if (end) *end.h() = g(e);
    if (v > INT32_MAX) { errno = ERANGE; return INT32_MAX; }
    if (v < INT32_MIN) { errno = ERANGE; return INT32_MIN; }
    return static_cast<int32_t>(v);
}
uint32_t g_strtoul(GStr s, GPtr<uint32_t> end, int32_t base) {
    char* e;
    unsigned long v = strtoul(s.h(), &e, base);
    if (end) *end.h() = g(e);
    if (v > UINT32_MAX && v < 0xFFFFFFFF00000000ul) { errno = ERANGE; return UINT32_MAX; }
    return static_cast<uint32_t>(v);
}
int64_t g_strtoll(GStr s, GPtr<uint32_t> end, int32_t base) {
    char* e;
    long long v = strtoll(s.h(), &e, base);
    if (end) *end.h() = g(e);
    return v;
}
uint64_t g_strtoull(GStr s, GPtr<uint32_t> end, int32_t base) {
    char* e;
    unsigned long long v = strtoull(s.h(), &e, base);
    if (end) *end.h() = g(e);
    return v;
}
double g_strtod(GStr s, GPtr<uint32_t> end) {
    char* e;
    double v = strtod(s.h(), &e);
    if (end) *end.h() = g(e);
    return v;
}

// sscanf: pointers that address same-size objects are translated in place; 32-bit `long`,
// `size_t`, `ptrdiff_t` and `%p` targets go through 64-bit temporaries.
void g_sscanf(cpu::Ctx& c) {
    const char* str = hp<char>(c.arg(0));
    const char* fmt = hp<char>(c.arg(1));
    void* ptrs[32] = {};
    struct Fix { int64_t v; uint32_t ga; bool is_unsigned; };
    Fix fixes[32];
    int np = 0, nf = 0, argi = 2;
    for (const char* p = fmt; *p && np < 32; ++p) {
        if (*p != '%') continue;
        ++p;
        if (*p == '%') continue;
        bool suppress = *p == '*';
        if (suppress) ++p;
        while (*p >= '0' && *p <= '9') ++p;
        char len = 0;
        if (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'j' || *p == 'z' || *p == 't' || *p == 'q') {
            len = *p++;
            if ((len == 'h' && *p == 'h') || (len == 'l' && *p == 'l')) { len = len == 'l' ? 'q' : 'H'; ++p; }
        }
        char conv = *p;
        if (conv == '[') {
            ++p;
            if (*p == '^') ++p;
            if (*p == ']') ++p;
            while (*p && *p != ']') ++p;
        }
        if (suppress) continue;
        uint32_t ga = c.arg(argi++);
        bool integer = strchr("diouxXn", conv) != nullptr;
        bool widen = (integer && (len == 'l' || len == 'z' || len == 't')) || conv == 'p';
        if (widen) {
            int32_t cur;
            memcpy(&cur, h(ga), 4);
            fixes[nf] = {cur, ga, conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o' || conv == 'p'};
            ptrs[np++] = &fixes[nf++].v;
        } else {
            ptrs[np++] = h(ga);
        }
    }
    int r = sscanf(str, fmt, ptrs[0], ptrs[1], ptrs[2], ptrs[3], ptrs[4], ptrs[5], ptrs[6], ptrs[7], ptrs[8], ptrs[9],
                   ptrs[10], ptrs[11], ptrs[12], ptrs[13], ptrs[14], ptrs[15], ptrs[16], ptrs[17], ptrs[18], ptrs[19],
                   ptrs[20], ptrs[21], ptrs[22], ptrs[23], ptrs[24], ptrs[25], ptrs[26], ptrs[27], ptrs[28], ptrs[29],
                   ptrs[30], ptrs[31]);
    for (int i = 0; i < nf; ++i) {
        uint32_t v = static_cast<uint32_t>(fixes[i].v);
        memcpy(h(fixes[i].ga), &v, 4);
    }
    c.ret(static_cast<uint32_t>(r));
}

// ---------------------------------------------------------------- guest callbacks and Android logging
void g_qsort(cpu::Ctx& c) {
    uint32_t base = c.arg(0), count = c.arg(1), width = c.arg(2), compare = c.arg(3);
    if (!base || !compare || !width || count < 2) { c.ret(0); return; }
    char* p = hp<char>(base);
    // Sort element indices so the guest comparator receives pointers to intact
    // guest objects; sorting a flat char range would compare individual bytes.
    std::vector<uint32_t> order(count);
    for (uint32_t i = 0; i < count; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [=](uint32_t a, uint32_t b) {
        uint32_t ga = base + a * width;
        uint32_t gb = base + b * width;
        return static_cast<int32_t>(cpu::call(compare, {ga, gb})) < 0;
    });
    std::vector<char> sorted(size_t{count} * width);
    for (uint32_t i = 0; i < count; ++i) {
        memcpy(sorted.data() + size_t{i} * width, p + size_t{order[i]} * width, width);
    }
    memcpy(p, sorted.data(), sorted.size());
    c.ret(0);
}
void g_bsearch(cpu::Ctx& c) {
    uint32_t key = c.arg(0), base = c.arg(1), count = c.arg(2), width = c.arg(3), compare = c.arg(4);
    if (!key || !base || !compare || !width) { c.ret(0); return; }
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t item = base + mid * width;
        int32_t order = static_cast<int32_t>(cpu::call(compare, {key, item}));
        if (order < 0) hi = mid;
        else if (order > 0) lo = mid + 1;
        else { c.ret(item); return; }
    }
    c.ret(0);
}
void g_android_log_print(cpu::Ctx& c) {
    int32_t prio = static_cast<int32_t>(c.arg(0));
    const char* tag = hp<const char>(c.arg(1));
    const char* fmt = hp<const char>(c.arg(2));
    __android_log_write(prio, tag ? tag : "guest", fmt ? fmt : "");
    c.ret(0);
}
void g_android_log_assert(cpu::Ctx& c) {
    const char* cond = hp<const char>(c.arg(0));
    const char* tag = hp<const char>(c.arg(1));
    const char* fmt = hp<const char>(c.arg(2));
    __android_log_write(ANDROID_LOG_FATAL, tag ? tag : "guest",
                        cond && *cond ? cond : (fmt ? fmt : "assertion failed"));
    std::abort();
}
int32_t g_mmap2(GPtr<void> addr, uint32_t len, int32_t prot, int32_t flags, int32_t fd, uint32_t page_off) {
    return static_cast<int32_t>(g_mmap(addr, len, prot, flags, fd, page_off << 12));
}
int32_t g_usleep(uint32_t usec) { return usleep(usec); }
uint32_t g_sleep(uint32_t seconds) { return sleep(seconds); }
uint32_t g_get_h_errno() { static thread_local uint32_t addr = arena::static_alloc(4, 4); return addr; }

// ---------------------------------------------------------------- math (softfp: doubles in core register pairs)
#define M1(n) double m_##n(double x) { return ::n(x); }
#define M1F(n) float m_##n(float x) { return ::n(x); }
#define M2(n) double m_##n(double x, double y) { return ::n(x, y); }
#define M2F(n) float m_##n(float x, float y) { return ::n(x, y); }
M1(acos) M1(asin) M1(atan) M1(ceil) M1(cos) M1(cosh) M1(exp) M1(floor) M1(log) M1(log10) M1(rint) M1(sin)
M1(sinh) M1(sqrt) M1(tan) M1(tanh) M1(trunc)
M1F(acosf) M1F(asinf) M1F(atanf) M1F(ceilf) M1F(cosf) M1F(expf) M1F(fabsf) M1F(floorf) M1F(logf) M1F(sinf) M1F(sqrtf) M1F(tanf)
M2(atan2) M2(fmod) M2(pow) M2F(atan2f) M2F(fmodf) M2F(powf)
double m_ldexp(double x, int32_t e) { return ldexp(x, e); }
double m_frexp(double x, GPtr<int32_t> e) { int ee; double r = frexp(x, &ee); *e.h() = ee; return r; }
double m_modf(double x, GPtr<double> ip) { double i; double r = modf(x, &i); memcpy(ip.h(), &i, 8); return r; }
int32_t m_isnan(double x) { return std::isnan(x); }
int32_t m_isinf(double x) { return std::isinf(x); }
int32_t m_lrint(double x) { return static_cast<int32_t>(lrint(x)); }
int32_t g_lrand48() { return static_cast<int32_t>(lrand48()); }
void g_srand48(int32_t s) { srand48(s); }

// ---------------------------------------------------------------- time
struct GTimeval { int32_t sec, usec; };
struct GTimespec { int32_t sec, nsec; };

struct GuestSem { sem_t host{}; };
std::mutex g_sem_mu;
std::unordered_map<uint32_t, std::shared_ptr<GuestSem>> g_semaphores;

std::shared_ptr<GuestSem> find_guest_sem(uint32_t address) {
    std::lock_guard lock(g_sem_mu);
    auto it = g_semaphores.find(address);
    if (it == g_semaphores.end()) { errno = EINVAL; return {}; }
    return it->second;
}

int32_t g_sem_init(GPtr<uint32_t> sem, int32_t pshared, uint32_t value) {
    if (!sem) { errno = EINVAL; return -1; }
    // The guest allocation is private to this emulated process. Do not claim
    // process-shared semantics that cannot be provided by the address map.
    if (pshared != 0) { errno = ENOSYS; return -1; }
    auto object = std::make_shared<GuestSem>();
    std::lock_guard lock(g_sem_mu);
    if (g_semaphores.contains(sem.a)) { errno = EBUSY; return -1; }
    if (sem_init(&object->host, 0, value) != 0) return -1;
    g_semaphores.emplace(sem.a, std::move(object));
    return 0;
}
int32_t g_sem_destroy(GPtr<uint32_t> sem) {
    if (!sem) { errno = EINVAL; return -1; }
    std::lock_guard lock(g_sem_mu);
    auto it = g_semaphores.find(sem.a);
    if (it == g_semaphores.end()) { errno = EINVAL; return -1; }
    if (sem_destroy(&it->second->host) != 0) return -1;
    g_semaphores.erase(it);
    return 0;
}
int32_t g_sem_getvalue(GPtr<uint32_t> sem, GPtr<int32_t> value) {
    if (!sem || !value) { errno = EINVAL; return -1; }
    auto object = find_guest_sem(sem.a);
    return object ? sem_getvalue(&object->host, value.h()) : -1;
}
int32_t g_sem_post(GPtr<uint32_t> sem) {
    if (!sem) { errno = EINVAL; return -1; }
    auto object = find_guest_sem(sem.a);
    return object ? sem_post(&object->host) : -1;
}
int32_t g_sem_wait(GPtr<uint32_t> sem) {
    if (!sem) { errno = EINVAL; return -1; }
    auto object = find_guest_sem(sem.a);
    return object ? sem_wait(&object->host) : -1;
}
int32_t g_sem_timedwait(GPtr<uint32_t> sem, GPtr<const GTimespec> timeout) {
    if (!sem || !timeout) { errno = EINVAL; return -1; }
    auto object = find_guest_sem(sem.a);
    if (!object) return -1;
    const GTimespec guest = *timeout.h();
    struct timespec host{guest.sec, guest.nsec};
    return sem_timedwait(&object->host, &host);
}
struct GTm { int32_t sec, min, hour, mday, mon, year, wday, yday, isdst, gmtoff; uint32_t zone; };

void tm_to_guest(const struct tm& t, GTm* o) {
    *o = {t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst,
          static_cast<int32_t>(t.tm_gmtoff), t.tm_zone ? interned(t.tm_zone) : 0};
}
struct tm tm_from_guest(const GTm* o) {
    struct tm t{};
    t.tm_sec = o->sec; t.tm_min = o->min; t.tm_hour = o->hour; t.tm_mday = o->mday; t.tm_mon = o->mon;
    t.tm_year = o->year; t.tm_wday = o->wday; t.tm_yday = o->yday; t.tm_isdst = o->isdst; t.tm_gmtoff = o->gmtoff;
    t.tm_zone = o->zone ? hp<const char>(o->zone) : nullptr;
    return t;
}

int32_t g_time(GPtr<int32_t> t) {
    int32_t v = static_cast<int32_t>(time(nullptr));
    if (t) *t.h() = v;
    return v;
}
int32_t g_gettimeofday(GPtr<GTimeval> tv, GPtr<void>) {
    struct timeval t;
    int r = gettimeofday(&t, nullptr);
    if (tv) *tv.h() = {static_cast<int32_t>(t.tv_sec), static_cast<int32_t>(t.tv_usec)};
    return r;
}
int32_t g_clock_gettime(int32_t clk, GPtr<GTimespec> ts) {
    struct timespec t;
    int r = clock_gettime(clk, &t);
    if (r == 0 && ts) *ts.h() = {static_cast<int32_t>(t.tv_sec), static_cast<int32_t>(t.tv_nsec)};
    return r;
}
int32_t g_nanosleep(GPtr<const GTimespec> req, GPtr<GTimespec> rem) {
    struct timespec rq{req.h()->sec, req.h()->nsec}, rm{};
    int r = nanosleep(&rq, &rm);
    if (rem) *rem.h() = {static_cast<int32_t>(rm.tv_sec), static_cast<int32_t>(rm.tv_nsec)};
    return r;
}
int32_t g_clock() { return static_cast<int32_t>(clock()); }
double g_difftime(int32_t a, int32_t b) { return static_cast<double>(a) - static_cast<double>(b); }
GPtr<GTm> g_gmtime_r(GPtr<const int32_t> t, GPtr<GTm> out) {
    time_t tt = *t.h();
    struct tm r;
    if (!gmtime_r(&tt, &r)) return {0};
    tm_to_guest(r, out.h());
    return out;
}
GPtr<GTm> g_localtime_r(GPtr<const int32_t> t, GPtr<GTm> out) {
    time_t tt = *t.h();
    struct tm r;
    if (!localtime_r(&tt, &r)) return {0};
    tm_to_guest(r, out.h());
    return out;
}
uint32_t g_tm_static() {
    static uint32_t a = arena::static_alloc(sizeof(GTm), 4);
    return a;
}
GPtr<GTm> g_gmtime(GPtr<const int32_t> t) { return g_gmtime_r(t, {g_tm_static()}); }
GPtr<GTm> g_localtime(GPtr<const int32_t> t) { return g_localtime_r(t, {g_tm_static()}); }
int32_t g_mktime(GPtr<GTm> t) {
    struct tm ht = tm_from_guest(t.h());
    time_t r = mktime(&ht);
    tm_to_guest(ht, t.h());
    return static_cast<int32_t>(r);
}
uint32_t g_strftime(GPtr<char> s, uint32_t max, GStr fmt, GPtr<const GTm> t) {
    struct tm ht = tm_from_guest(t.h());
    return static_cast<uint32_t>(strftime(s.h(), max, fmt.h(), &ht));
}
uint32_t g_wcsftime(GPtr<wchar_t> s, uint32_t max, GPtr<const wchar_t> fmt, GPtr<const GTm> t) {
    struct tm ht = tm_from_guest(t.h());
    return static_cast<uint32_t>(wcsftime(s.h(), max, fmt.h(), &ht));
}
int32_t g_utime(GStr path, GPtr<const int32_t> times) {
    if (!times) return utime(path.h(), nullptr);
    struct utimbuf u{times.h()[0], times.h()[1]};
    return utime(path.h(), &u);
}
int32_t g_utimes(GStr path, GPtr<const GTimeval> times) {
    if (!times) return utimes(path.h(), nullptr);
    struct timeval tv[2] = {
        {times.h()[0].sec, times.h()[0].usec},
        {times.h()[1].sec, times.h()[1].usec}
    };
    return utimes(path.h(), tv);
}

// ---------------------------------------------------------------- file system
int host_open_flags(int32_t f) {
    // ARM and arm64 share these values; x86_64 uses the asm-generic ones.
    constexpr int kDirectory = 040000, kNoFollow = 0100000, kDirect = 0200000, kLargeFile = 0400000;
    int r = f & ~(kDirectory | kNoFollow | kDirect | kLargeFile);
    if (f & kDirectory) r |= O_DIRECTORY;
    if (f & kNoFollow) r |= O_NOFOLLOW;
    if (f & kDirect) r |= O_DIRECT;
    return r;
}
int32_t guest_open_flags(int f) {
    int32_t r = f & ~(O_DIRECTORY | O_NOFOLLOW | O_DIRECT);
    if (f & O_DIRECTORY) r |= 040000;
    if (f & O_NOFOLLOW) r |= 0100000;
    if (f & O_DIRECT) r |= 0200000;
    return r;
}

int32_t g_open(GStr path, int32_t flags, int32_t mode) {
    const char* p = path.h();
    bridge_log("IO: open begin path=%s flags=0x%x", p ? p : "(null)", flags);
    int32_t fd = open(p, host_open_flags(flags), mode);
    bridge_log("IO: open end path=%s fd=%d errno=%d", p ? p : "(null)", fd, fd < 0 ? errno : 0);
    return fd;
}
int32_t g_close(int32_t fd) { return close(fd); }
int32_t g_read(int32_t fd, GPtr<void> b, uint32_t n) {
    char fd_path[64] = {};
    char target[PATH_MAX] = {};
    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
    ssize_t target_len = readlink(fd_path, target, sizeof(target) - 1);
    if (target_len >= 0) target[target_len] = '\0';
    else snprintf(target, sizeof(target), "(readlink errno=%d)", errno);
    struct stat st = {};
    int stat_result = fstat(fd, &st);
    bridge_log("IO: read begin fd=%d target=%s size=%u mode=0%o stat=%d", fd, target, n,
               stat_result == 0 ? st.st_mode : 0, stat_result);
    int32_t result = static_cast<int32_t>(read(fd, b.h(), n));
    bridge_log("IO: read end fd=%d target=%s result=%d errno=%d", fd, target, result,
               result < 0 ? errno : 0);
    return result;
}
int32_t g_write(int32_t fd, GPtr<const void> b, uint32_t n) { return static_cast<int32_t>(write(fd, b.h(), n)); }
int32_t g_lseek(int32_t fd, int32_t off, int32_t whence) { return static_cast<int32_t>(lseek(fd, off, whence)); }
int64_t g_lseek64(int32_t fd, int64_t off, int32_t whence) { return static_cast<int64_t>(lseek(fd, static_cast<off_t>(off), whence)); }
int32_t g_fsync(int32_t fd) { return fsync(fd); }
struct GuestRlimit { uint32_t cur, max; };
int32_t g_getrlimit(int32_t resource, GPtr<GuestRlimit> out) {
    struct rlimit host{};
    if (getrlimit(resource, &host) != 0) return -1;
    constexpr rlim_t kGuestInfinity = 0xffffffffu;
    out.h()->cur = host.rlim_cur == RLIM_INFINITY || host.rlim_cur > kGuestInfinity
        ? static_cast<uint32_t>(kGuestInfinity) : static_cast<uint32_t>(host.rlim_cur);
    out.h()->max = host.rlim_max == RLIM_INFINITY || host.rlim_max > kGuestInfinity
        ? static_cast<uint32_t>(kGuestInfinity) : static_cast<uint32_t>(host.rlim_max);
    return 0;
}
int32_t g_setrlimit(int32_t resource, GPtr<const GuestRlimit> in) {
    constexpr uint32_t kGuestInfinity = 0xffffffffu;
    struct rlimit host{};
    host.rlim_cur = in.h()->cur == kGuestInfinity ? RLIM_INFINITY : in.h()->cur;
    host.rlim_max = in.h()->max == kGuestInfinity ? RLIM_INFINITY : in.h()->max;
    return setrlimit(resource, &host);
}
int32_t g_uname(GPtr<uint8_t> out) {
    struct utsname host{};
    if (uname(&host) != 0) return -1;
    // ARM32 bionic utsname: six fixed 65-byte NUL-terminated fields.
    static_assert(sizeof(host.sysname) == 65, "unexpected host utsname field size");
    uint8_t* p = out.h();
    memcpy(p + 0, host.sysname, 65);
    memcpy(p + 65, host.nodename, 65);
    memcpy(p + 130, host.release, 65);
    memcpy(p + 195, host.version, 65);
    memcpy(p + 260, host.machine, 65);
    memcpy(p + 325, host.domainname, 65);
    return 0;
}
int32_t g_flock(int32_t fd, int32_t operation) { return flock(fd, operation); }
int32_t g_dup(int32_t fd) { return dup(fd); }
int32_t g_dup2(int32_t oldfd, int32_t newfd) { return dup2(oldfd, newfd); }
int32_t g_fchmod(int32_t fd, uint32_t mode) { return fchmod(fd, mode); }
int32_t g_fchown(int32_t fd, uint32_t owner, uint32_t group) { return fchown(fd, owner, group); }
int32_t g_ftruncate(int32_t fd, int64_t length) { return ftruncate(fd, static_cast<off_t>(length)); }
int32_t g_chdir(GStr path) { return chdir(path.h()); }
int32_t g_readlink(GStr path, GPtr<char> buf, uint32_t size) { return static_cast<int32_t>(readlink(path.h(), buf.h(), size)); }
int32_t g_gettid() {
#ifdef SYS_gettid
    return static_cast<int32_t>(syscall(SYS_gettid));
#else
    return -1;
#endif
}
int32_t g_pipe(GPtr<int32_t> fds) { return pipe(reinterpret_cast<int*>(fds.h())); }
int32_t g_poll(GPtr<struct pollfd> fds, uint32_t n, int32_t timeout) { return poll(fds.h(), n, timeout); }
int32_t g_writev(int32_t fd, GPtr<const uint32_t> iov, int32_t cnt) {
    std::vector<struct iovec> v(static_cast<size_t>(cnt > 0 ? cnt : 0));
    for (int32_t i = 0; i < cnt; ++i)
        v[static_cast<size_t>(i)] = {h(iov.h()[2 * i]), iov.h()[2 * i + 1]};
    return static_cast<int32_t>(writev(fd, v.data(), cnt));
}
int32_t g_access(GStr p, int32_t m) { return access(p.h(), m); }
int32_t g_chmod(GStr p, uint32_t m) { return chmod(p.h(), m); }
int32_t g_mkdir(GStr p, uint32_t m) { return mkdir(p.h(), m); }
int32_t g_rmdir(GStr p) { return rmdir(p.h()); }
int32_t g_unlink(GStr p) { return unlink(p.h()); }
int32_t g_remove(GStr p) { return remove(p.h()); }
int32_t g_rename(GStr a, GStr b) { return rename(a.h(), b.h()); }
int32_t g_pathconf(GStr p, int32_t n) { return static_cast<int32_t>(pathconf(p.h(), n)); }
GPtr<char> g_getcwd(GPtr<char> buf, uint32_t size) {
    char tmp[PATH_MAX];
    if (!getcwd(tmp, sizeof tmp)) return {0};
    if (!buf) return {arena::gstrdup(tmp)};
    if (strlen(tmp) + 1 > size) { errno = ERANGE; return {0}; }
    strcpy(buf.h(), tmp);
    return buf;
}

int32_t g_fcntl(int32_t fd, int32_t cmd, uint32_t arg) {
    switch (cmd) {
    case F_GETFL: { int r = fcntl(fd, F_GETFL); return r < 0 ? r : guest_open_flags(r); }
    case F_SETFL: return fcntl(fd, F_SETFL, host_open_flags(static_cast<int32_t>(arg)));
    case F_GETFD: case F_SETFD: case F_DUPFD: case F_DUPFD_CLOEXEC: return fcntl(fd, cmd, static_cast<int32_t>(arg));
    default:
        LOGW("fcntl cmd %d unsupported", cmd);
        errno = EINVAL;
        return -1;
    }
}
int32_t g_ioctl(int32_t fd, uint32_t req, uint32_t arg) {
    // FIONBIO, FIONREAD and most others take a pointer to an int-sized object.
    return ioctl(fd, req, arg ? h(arg) : nullptr);
}

void put_stat(const struct stat& s, uint8_t* o) {
    memset(o, 0, 104);
    auto p32 = [&](int off, uint32_t v) { memcpy(o + off, &v, 4); };
    auto p64 = [&](int off, uint64_t v) { memcpy(o + off, &v, 8); };
    p64(0, s.st_dev); p32(12, static_cast<uint32_t>(s.st_ino)); p32(16, s.st_mode); p32(20, static_cast<uint32_t>(s.st_nlink));
    p32(24, s.st_uid); p32(28, s.st_gid); p64(32, s.st_rdev); p64(48, static_cast<uint64_t>(s.st_size));
    p32(56, static_cast<uint32_t>(s.st_blksize)); p64(64, static_cast<uint64_t>(s.st_blocks));
    p32(72, static_cast<uint32_t>(s.st_atim.tv_sec)); p32(76, static_cast<uint32_t>(s.st_atim.tv_nsec));
    p32(80, static_cast<uint32_t>(s.st_mtim.tv_sec)); p32(84, static_cast<uint32_t>(s.st_mtim.tv_nsec));
    p32(88, static_cast<uint32_t>(s.st_ctim.tv_sec)); p32(92, static_cast<uint32_t>(s.st_ctim.tv_nsec));
    p64(96, s.st_ino);
}
int32_t g_stat(GStr p, GPtr<uint8_t> out) {
    struct stat s;
    int r = stat(p.h(), &s);
    if (r == 0) put_stat(s, out.h());
    return r;
}
int32_t g_fstat(int32_t fd, GPtr<uint8_t> out) {
    struct stat s;
    int r = fstat(fd, &s);
    if (r == 0) put_stat(s, out.h());
    return r;
}
int32_t g_statfs(GStr p, GPtr<uint8_t> out) {
    struct statfs s;
    int r = statfs(p.h(), &s);
    if (r != 0) return r;
    uint8_t* o = out.h();
    memset(o, 0, 84);
    auto p32 = [&](int off, uint32_t v) { memcpy(o + off, &v, 4); };
    auto p64 = [&](int off, uint64_t v) { memcpy(o + off, &v, 8); };
    p32(0, static_cast<uint32_t>(s.f_type)); p32(4, static_cast<uint32_t>(s.f_bsize));
    p64(8, s.f_blocks); p64(16, s.f_bfree); p64(24, s.f_bavail); p64(32, s.f_files); p64(40, s.f_ffree);
    memcpy(o + 48, &s.f_fsid, 8);
    p32(56, static_cast<uint32_t>(s.f_namelen)); p32(60, static_cast<uint32_t>(s.f_frsize));
    p32(64, static_cast<uint32_t>(s.f_flags));
    return 0;
}

std::mutex g_dir_mu;
std::unordered_map<uint32_t, DIR*> g_dirs;
uint32_t g_opendir(GStr p) {
    DIR* d = opendir(p.h());
    if (!d) return 0;
    uint32_t token = arena::gmalloc(16);
    std::lock_guard l(g_dir_mu);
    g_dirs[token] = d;
    return token;
}
DIR* host_dir(uint32_t t) {
    std::lock_guard l(g_dir_mu);
    auto it = g_dirs.find(t);
    return it == g_dirs.end() ? nullptr : it->second;
}
int32_t g_closedir(uint32_t t) {
    DIR* d;
    {
        std::lock_guard l(g_dir_mu);
        auto it = g_dirs.find(t);
        if (it == g_dirs.end()) return -1;
        d = it->second;
        g_dirs.erase(it);
    }
    arena::gfree(t);
    return closedir(d);
}
int32_t g_readdir_r(uint32_t t, GPtr<uint8_t> entry, GPtr<uint32_t> result) {
    DIR* d = host_dir(t);
    if (!d) return EBADF;
    errno = 0;
    struct dirent* e = readdir(d);
    if (!e) { *result.h() = 0; return errno; }
    static_assert(sizeof(struct dirent) == 280, "bionic dirent layout");
    memcpy(entry.h(), e, sizeof(struct dirent));
    *result.h() = entry.a;
    return 0;
}
GPtr<uint8_t> g_readdir(uint32_t t) {
    DIR* d = host_dir(t);
    if (!d) { errno = EBADF; return {0}; }
    static_assert(sizeof(struct dirent) == 280, "bionic dirent layout");
    static thread_local uint32_t entry = arena::static_alloc(sizeof(struct dirent), 4);
    errno = 0;
    struct dirent* e = readdir(d);
    if (!e) return {0};
    memcpy(h(entry), e, sizeof(struct dirent));
    return {entry};
}

// ARM32 addrinfo uses 32-bit pointers and size_t; translate every node rather
// than exposing the host's 64-bit struct layout to guest code.
struct GAddrInfo {
    int32_t flags, family, socktype, protocol;
    uint32_t addrlen, addr, canonname, next;
};
static_assert(sizeof(GAddrInfo) == 32);
std::mutex g_addrinfo_mu;
std::unordered_map<uint32_t, std::vector<uint32_t>> g_addrinfo_allocations;

int32_t g_getaddrinfo(GStr node, GStr service, GPtr<const GAddrInfo> guest_hints, GPtr<uint32_t> guest_result) {
    if (!guest_result) return EAI_FAIL;
    *guest_result.h() = 0;
    struct addrinfo hints{};
    struct addrinfo* host_result = nullptr;
    if (guest_hints) {
        const auto& in = *guest_hints.h();
        hints.ai_flags = in.flags;
        hints.ai_family = in.family;
        hints.ai_socktype = in.socktype;
        hints.ai_protocol = in.protocol;
    }
    int status = getaddrinfo(node ? node.h() : nullptr, service ? service.h() : nullptr,
                             guest_hints ? &hints : nullptr, &host_result);
    if (status != 0) return status;

    std::vector<uint32_t> allocations;
    uint32_t head = 0, tail = 0;
    auto allocate = [&](uint32_t bytes) -> uint32_t {
        uint32_t address = arena::gcalloc(1, bytes);
        if (address) allocations.push_back(address);
        return address;
    };
    for (const struct addrinfo* item = host_result; item; item = item->ai_next) {
        uint32_t guest_node = allocate(sizeof(GAddrInfo));
        uint32_t guest_addr = item->ai_addrlen ? allocate(static_cast<uint32_t>(item->ai_addrlen)) : 0;
        uint32_t guest_name = item->ai_canonname ? allocate(static_cast<uint32_t>(strlen(item->ai_canonname) + 1)) : 0;
        if (!guest_node || (item->ai_addrlen && !guest_addr) || (item->ai_canonname && !guest_name)) {
            for (uint32_t address : allocations) arena::gfree(address);
            freeaddrinfo(host_result);
            return EAI_MEMORY;
        }
        if (guest_addr) memcpy(h(guest_addr), item->ai_addr, item->ai_addrlen);
        if (guest_name) memcpy(h(guest_name), item->ai_canonname, strlen(item->ai_canonname) + 1);
        auto* out = hp<GAddrInfo>(guest_node);
        out->flags = item->ai_flags;
        out->family = item->ai_family;
        out->socktype = item->ai_socktype;
        out->protocol = item->ai_protocol;
        out->addrlen = static_cast<uint32_t>(item->ai_addrlen);
        out->addr = guest_addr;
        out->canonname = guest_name;
        if (!head) head = guest_node;
        if (tail) hp<GAddrInfo>(tail)->next = guest_node;
        tail = guest_node;
    }
    freeaddrinfo(host_result);
    if (head) {
        std::lock_guard lock(g_addrinfo_mu);
        g_addrinfo_allocations.emplace(head, std::move(allocations));
    }
    *guest_result.h() = head;
    return 0;
}
void g_freeaddrinfo(GPtr<GAddrInfo> result) {
    if (!result) return;
    std::vector<uint32_t> allocations;
    {
        std::lock_guard lock(g_addrinfo_mu);
        auto it = g_addrinfo_allocations.find(result.a);
        if (it == g_addrinfo_allocations.end()) return;
        allocations = std::move(it->second);
        g_addrinfo_allocations.erase(it);
    }
    for (uint32_t address : allocations) arena::gfree(address);
}

// Legacy resolver results contain 32-bit guest pointers. Keep a per-thread
// guest-layout snapshot, valid until the next host resolver call on this thread.
struct GHostEnt { uint32_t name, aliases; int32_t addrtype, length; uint32_t addr_list; };
uint32_t copy_hostent(const struct hostent* in) {
    if (!in) return 0;
    constexpr uint32_t kName = 20;
    constexpr uint32_t kAliases = kName + 256;
    constexpr uint32_t kAddresses = kAliases + 16 * 4;
    constexpr uint32_t kAliasText = kAddresses + 16 * 4;
    constexpr uint32_t kAddressText = kAliasText + 16 * 256;
    constexpr uint32_t kBytes = kAddressText + 16 * 16;
    static thread_local uint32_t block = arena::static_alloc(kBytes, 4);
    if (!block) return 0;
    memset(h(block), 0, kBytes);
    auto* out = hp<GHostEnt>(block);
    out->name = block + kName;
    out->aliases = block + kAliases;
    out->addrtype = in->h_addrtype;
    out->length = in->h_length;
    out->addr_list = block + kAddresses;
    if (in->h_name) strncpy(reinterpret_cast<char*>(h(block + kName)), in->h_name, 255);
    auto* aliases = hp<uint32_t>(block + kAliases);
    if (in->h_aliases) for (uint32_t i = 0; i < 15 && in->h_aliases[i]; ++i) {
        aliases[i] = block + kAliasText + i * 256;
        strncpy(reinterpret_cast<char*>(h(aliases[i])), in->h_aliases[i], 255);
    }
    auto* addresses = hp<uint32_t>(block + kAddresses);
    if (in->h_addr_list && in->h_length > 0 && in->h_length <= 16) {
        for (uint32_t i = 0; i < 15 && in->h_addr_list[i]; ++i) {
            addresses[i] = block + kAddressText + i * 16;
            memcpy(h(addresses[i]), in->h_addr_list[i], in->h_length);
        }
    }
    return block;
}
uint32_t g_gethostbyname(GStr name) { return copy_hostent(gethostbyname(name.h())); }
uint32_t g_gethostbyname2(GStr name, int32_t family) { return copy_hostent(gethostbyname2(name.h(), family)); }
uint32_t g_gethostbyaddr(GPtr<const void> address, uint32_t length, int32_t type) {
    return copy_hostent(gethostbyaddr(address.h(), static_cast<socklen_t>(length), type));
}

struct GProtoEnt { uint32_t name, aliases; int32_t proto; };
uint32_t copy_protoent(const struct protoent* in) {
    if (!in) return 0;
    constexpr uint32_t kName = 12;
    constexpr uint32_t kAliases = kName + 256;
    constexpr uint32_t kAliasText = kAliases + 16 * 4;
    constexpr uint32_t kBytes = kAliasText + 16 * 256;
    static thread_local uint32_t block = arena::static_alloc(kBytes, 4);
    if (!block) return 0;
    memset(h(block), 0, kBytes);
    auto* out = hp<GProtoEnt>(block);
    out->name = block + kName;
    out->aliases = block + kAliases;
    out->proto = in->p_proto;
    if (in->p_name) strncpy(reinterpret_cast<char*>(h(block + kName)), in->p_name, 255);
    auto* aliases = hp<uint32_t>(block + kAliases);
    if (in->p_aliases) for (uint32_t i = 0; i < 15 && in->p_aliases[i]; ++i) {
        aliases[i] = block + kAliasText + i * 256;
        strncpy(reinterpret_cast<char*>(h(aliases[i])), in->p_aliases[i], 255);
    }
    return block;
}
uint32_t g_getprotobyname(GStr name) { return copy_protoent(getprotobyname(name.h())); }
uint32_t g_getprotobynumber(int32_t number) { return copy_protoent(getprotobynumber(number)); }

int32_t g_getnameinfo(GPtr<const sockaddr> address, uint32_t address_length,
                      GPtr<char> host, uint32_t host_length,
                      GPtr<char> service, uint32_t service_length, int32_t flags) {
    return getnameinfo(address.h(), static_cast<socklen_t>(address_length),
                       host ? host.h() : nullptr, host_length,
                       service ? service.h() : nullptr, service_length, flags);
}

// ---------------------------------------------------------------- sockets (sockaddr layouts are ABI-neutral)
int32_t g_socket(int32_t d, int32_t t, int32_t p) { return socket(d, t, p); }

int32_t g_accept(int32_t fd, GPtr<sockaddr> address, GPtr<socklen_t> length) {
    return accept(fd, address ? address.h() : nullptr, length ? length.h() : nullptr);
}
int32_t g_listen(int32_t fd, int32_t backlog) { return listen(fd, backlog); }
int32_t g_sendto(int32_t fd, GPtr<const void> buffer, uint32_t length, int32_t flags,
                 GPtr<const sockaddr> address, uint32_t address_length) {
    return static_cast<int32_t>(sendto(fd, buffer.h(), length, flags | MSG_NOSIGNAL,
                                       address ? address.h() : nullptr, address_length));
}
int32_t g_shutdown(int32_t fd, int32_t how) { return shutdown(fd, how); }
int32_t g_socketpair(int32_t domain, int32_t type, int32_t protocol, GPtr<int32_t> pair) {
    return socketpair(domain, type, protocol, pair.h());
}
int32_t g_select(int32_t nfds, GPtr<uint8_t> read_bits, GPtr<uint8_t> write_bits,
                 GPtr<uint8_t> except_bits, GPtr<GTimeval> timeout) {
    static_assert(sizeof(fd_set) == 128, "unexpected Android fd_set ABI");
    fd_set r{}, w{}, e{};
    if (read_bits) memcpy(&r, read_bits.h(), sizeof(r));
    if (write_bits) memcpy(&w, write_bits.h(), sizeof(w));
    if (except_bits) memcpy(&e, except_bits.h(), sizeof(e));
    struct timeval t{}, *tp = nullptr;
    if (timeout) { t = {timeout.h()->sec, timeout.h()->usec}; tp = &t; }
    int result = select(nfds, read_bits ? &r : nullptr, write_bits ? &w : nullptr,
                        except_bits ? &e : nullptr, tp);
    if (read_bits) memcpy(read_bits.h(), &r, sizeof(r));
    if (write_bits) memcpy(write_bits.h(), &w, sizeof(w));
    if (except_bits) memcpy(except_bits.h(), &e, sizeof(e));
    if (timeout && tp) *timeout.h() = {static_cast<int32_t>(t.tv_sec), static_cast<int32_t>(t.tv_usec)};
    return result;
}
int32_t g_bind(int32_t fd, GPtr<const sockaddr> a, uint32_t l) { return bind(fd, a.h(), l); }
int32_t g_connect(int32_t fd, GPtr<const sockaddr> a, uint32_t l) { return connect(fd, a.h(), l); }
int32_t g_getsockname(int32_t fd, GPtr<sockaddr> a, GPtr<socklen_t> l) { return getsockname(fd, a.h(), l.h()); }
int32_t g_getpeername(int32_t fd, GPtr<sockaddr> a, GPtr<socklen_t> l) { return getpeername(fd, a.h(), l.h()); }
int32_t g_send(int32_t fd, GPtr<const void> b, uint32_t n, int32_t f) {
    return static_cast<int32_t>(send(fd, b.h(), n, f | MSG_NOSIGNAL));
}
int32_t g_recv(int32_t fd, GPtr<void> b, uint32_t n, int32_t f) { return static_cast<int32_t>(recv(fd, b.h(), n, f)); }
int32_t g_recvfrom(int32_t fd, GPtr<void> b, uint32_t n, int32_t f, GPtr<sockaddr> a, GPtr<socklen_t> l) {
    return static_cast<int32_t>(recvfrom(fd, b.h(), n, f, a.h(), l.h()));
}
bool is_timeo(int32_t level, int32_t name) { return level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO); }
int32_t g_setsockopt(int32_t fd, int32_t level, int32_t name, GPtr<const void> v, uint32_t l) {
    if (is_timeo(level, name) && l >= 8) {
        auto* gt = reinterpret_cast<const GTimeval*>(v.h());
        struct timeval t{gt->sec, gt->usec};
        return setsockopt(fd, level, name, &t, sizeof t);
    }
    return setsockopt(fd, level, name, v.h(), l);
}
int32_t g_getsockopt(int32_t fd, int32_t level, int32_t name, GPtr<void> v, GPtr<socklen_t> l) {
    if (is_timeo(level, name)) {
        struct timeval t{};
        socklen_t tl = sizeof t;
        int r = getsockopt(fd, level, name, &t, &tl);
        *reinterpret_cast<GTimeval*>(v.h()) = {static_cast<int32_t>(t.tv_sec), static_cast<int32_t>(t.tv_usec)};
        *l.h() = 8;
        return r;
    }
    return getsockopt(fd, level, name, v.h(), l.h());
}
uint32_t g_inet_addr(GStr s) { return inet_addr(s.h()); }
GPtr<char> g_inet_ntop(int32_t af, GPtr<const void> src, GPtr<char> dst, uint32_t n) {
    return inet_ntop(af, src.h(), dst.h(), n) ? dst : GPtr<char>{0};
}
int32_t g_inet_pton(int32_t af, GStr src, GPtr<void> dst) { return inet_pton(af, src.h(), dst.h()); }
int32_t g_gethostname(GPtr<char> b, uint32_t n) { return gethostname(b.h(), n); }

// ---------------------------------------------------------------- process / environment
int32_t g_getpid() { return getpid(); }
uint32_t g_getuid() { return getuid(); }
uint32_t g_geteuid() { return geteuid(); }
uint32_t g_getgid() { return getgid(); }
uint32_t g_getegid() { return getegid(); }
uint32_t g_getpwuid(uint32_t uid) {
    static uint32_t pw = arena::static_alloc(24, 4);
    struct passwd* p = getpwuid(uid);
    if (!p) return 0;
    auto* o = hp<uint32_t>(pw);
    o[0] = interned(p->pw_name ? p->pw_name : "");
    o[1] = interned("");
    o[2] = p->pw_uid;
    o[3] = p->pw_gid;
    o[4] = interned(p->pw_dir ? p->pw_dir : "/");
    o[5] = interned(p->pw_shell ? p->pw_shell : "");
    return pw;
}
GPtr<char> g_getenv(GStr name) {
    const char* v = getenv(name.h());
    return {v ? interned(v) : 0};
}
GPtr<char> g_setlocale(int32_t, GStr) { return {interned("C")}; }
int32_t g_sysconf(int32_t n) {
    long v = sysconf(n);
    return v > INT32_MAX ? INT32_MAX : static_cast<int32_t>(v);
}
uint32_t g_getauxval(uint32_t type) {
    constexpr uint32_t kHwcapArm = 1 | 2 | 4 | 16 | 64 | 128 | 4096 | 8192 | 32768 | 65536 | 131072 | 262144 | 524288;
    switch (type) {
    case 16: return kHwcapArm;  // AT_HWCAP: swp half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32
    case 26: return 0;          // AT_HWCAP2: no crypto extensions advertised
    case 6: return 4096;        // AT_PAGESZ
    default: return 0;
    }
}
// Translate the host 64-bit sysinfo layout into the ARM32 guest ABI layout.
int32_t g_sysinfo(GPtr<uint8_t> out) {
    struct sysinfo s{};
    if (sysinfo(&s) != 0) return -1;
    uint8_t* p = out.h();
    memset(p, 0, 64);
    auto p32 = [&](int off, uint32_t v) { memcpy(p + off, &v, 4); };
    p32(0, static_cast<uint32_t>(s.uptime));
    for (int i = 0; i < 3; ++i) p32(4 + i * 4, static_cast<uint32_t>(s.loads[i]));
    p32(16, static_cast<uint32_t>(s.totalram));
    p32(20, static_cast<uint32_t>(s.freeram));
    p32(24, static_cast<uint32_t>(s.sharedram));
    p32(28, static_cast<uint32_t>(s.bufferram));
    p32(32, static_cast<uint32_t>(s.totalswap));
    p32(36, static_cast<uint32_t>(s.freeswap));
    uint16_t procs = s.procs;
    memcpy(p + 40, &procs, sizeof(procs));
    p32(44, static_cast<uint32_t>(s.totalhigh));
    p32(48, static_cast<uint32_t>(s.freehigh));
    p32(52, static_cast<uint32_t>(s.mem_unit));
    return 0;
}
int32_t g_system_property_get(GStr name, GPtr<char> value) { return __system_property_get(name.h(), value.h()); }
int32_t g_getrandom(GPtr<void> buf, uint32_t len, uint32_t flags) {
    bridge_log("RANDOM: getrandom import requested len=%u flags=0x%x", len, flags);
#ifdef SYS_getrandom
    long r = syscall(SYS_getrandom, buf.h(), static_cast<size_t>(len), flags);
    bridge_log("RANDOM: getrandom import returned %ld errno=%d", r, r < 0 ? errno : 0);
    return static_cast<int32_t>(r);
#else
    errno = ENOSYS;
    bridge_log("RANDOM: host SYS_getrandom unavailable");
    return -1;
#endif
}
int32_t g_sched_yield() { return sched_yield(); }
int32_t g_sigaction(int32_t, GPtr<const void>, GPtr<uint8_t> old) {
    if (old) memset(old.h(), 0, 16);
    return 0;
}
int32_t g_sigprocmask(int32_t, GPtr<const void>, GPtr<uint8_t> old) {
    if (old) memset(old.h(), 0, 4);
    return 0;
}
void g_abort(cpu::Ctx& c) { fatal("guest abort() from %s", elf::describe(c.r(14)).c_str()); }
void g_exit(int32_t code) {
    LOGW("guest exit(%d)", code);
    exit(code);
}
int32_t g_raise(int32_t sig) {
    LOGW("guest raise(%d)", sig);
    if (sig == SIGABRT || sig == SIGSEGV || sig == SIGKILL) fatal("guest raised signal %d", sig);
    return 0;
}
void g_stack_chk_fail(cpu::Ctx& c) { fatal("guest stack smashing detected near %s", elf::describe(c.r(14)).c_str()); }
void g_assert2(GStr file, int32_t line, GStr function, GStr expression) {
    fatal("guest assertion failed: %s:%d: %s: %s", file ? file.h() : "?", line,
          function ? function.h() : "?", expression ? expression.h() : "?");
}
// These are compiler/runtime instrumentation markers, not synchronization primitives.
void g_google_potentially_blocking_region_begin() {}
void g_google_potentially_blocking_region_end() {}

void g_syscall(cpu::Ctx& c) {
    int32_t nr = static_cast<int32_t>(c.arg(0));
    bridge_log("SYSCALL: begin nr=%d a1=0x%x a2=0x%x a3=0x%x", nr, c.arg(1), c.arg(2), c.arg(3));
    long r;
    switch (nr) {
    case 20: r = getpid(); break;                    // getpid
    case 158: r = sched_yield(); break;              // sched_yield
    case 224: r = gettid(); break;                   // gettid
    case 263: {                                      // clock_gettime
        struct timespec t;
        r = clock_gettime(static_cast<int>(c.arg(1)), &t);
        if (r == 0) *hp<GTimespec>(c.arg(2)) = {static_cast<int32_t>(t.tv_sec), static_cast<int32_t>(t.tv_nsec)};
        break;
    }
    case 384: {                                      // getrandom (ARM EABI)
        uint32_t len = c.arg(2), flags = c.arg(3);
        bridge_log("RANDOM: syscall getrandom begin len=%u flags=0x%x", len, flags);
#ifdef SYS_getrandom
        r = syscall(SYS_getrandom, h(c.arg(1)), static_cast<size_t>(len), flags);
#else
        errno = ENOSYS;
        r = -1;
#endif
        bridge_log("RANDOM: syscall getrandom end result=%ld errno=%d", r, r < 0 ? errno : 0);
        break;
    }
    case 240: {                                      // futex(uaddr, op, val, timeout, uaddr2, val3)
        int op = static_cast<int>(c.arg(2));
        struct timespec ts, *tsp = nullptr;
        int cmd = op & FUTEX_CMD_MASK;
        if ((cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) && c.arg(4)) {
            auto* gt = hp<GTimespec>(c.arg(4));
            ts = {gt->sec, gt->nsec};
            tsp = &ts;
        }
        r = syscall(SYS_futex, h(c.arg(1)), op, c.arg(3), tsp, c.arg(5) ? h(c.arg(5)) : nullptr, c.arg(6));
        break;
    }
    default:
        LOGW("unsupported guest syscall %d from %s", nr, elf::describe(c.r(14)).c_str());
        errno = ENOSYS;
        r = -1;
    }
    bridge_log("SYSCALL: end nr=%d result=%ld errno=%d", nr, r, r < 0 ? errno : 0);
    if (r < 0) cpu::set_guest_errno(errno);
    c.ret(static_cast<uint32_t>(r));
}

// ---------------------------------------------------------------- dynamic linking
thread_local uint32_t t_dlerror = 0;
uint32_t g_dlopen(GStr name, int32_t) {
    LOGI("guest dlopen(%s)", name ? name.h() : "NULL");
    return 1;
}
uint32_t g_dlsym(uint32_t, GStr name) {
    std::string n = name.h();
    uint32_t a = elf::find_export(n);
    if (!a) a = hle::resolve(n);
    if (!a) {
        LOGW("guest dlsym(%s) not found", n.c_str());
        t_dlerror = interned("undefined symbol: " + n);
    }
    return a;
}
int32_t g_dlclose(uint32_t) { return 0; }
uint32_t g_dlerror() { uint32_t e = t_dlerror; t_dlerror = 0; return e; }

// ARM32 bionic Dl_info is four 32-bit guest pointers (16 bytes).
struct GDlInfo { uint32_t fname, fbase, sname, saddr; };
int32_t g_dladdr(GPtr<const void> address, GPtr<GDlInfo> info) {
    const elf::Module* module = elf::owner(address.a);
    if (!module) return 0;
    if (info) {
        GDlInfo out{};
        out.fname = interned(module->name);
        out.fbase = module->bias;
        uint32_t symbol_address = 0;
        std::string symbol_name;
        if (elf::lookup_symbol(address.a, &symbol_address, &symbol_name)) {
            out.sname = interned(symbol_name);
            out.saddr = symbol_address;
        }
        *info.h() = out;
    }
    return 1;
}

// ---------------------------------------------------------------- C++ runtime support
int32_t g_cxa_atexit(uint32_t, uint32_t, uint32_t) { return 0; }
void g_cxa_finalize(uint32_t) {}
uint32_t g_errno() { return cpu::thread_errno_addr(); }
uint32_t g_unwind_find_exidx(uint32_t pc, GPtr<int32_t> count) { return elf::find_exidx(pc, count.h()); }

// Old bionic (OpenBSD-derived) exports pointer variables: `const char* _ctype_ = _C_ctype_;`.
void add_table_pointer(const char* name, uint32_t table) {
    uint32_t var = arena::static_alloc(4, 4);
    *hp<uint32_t>(var) = table;
    hle::add_data(name, var);
}

void make_data() {
    // BSD-style ctype table used by bionic's ctype macros: _ctype_[1 + c].
    enum { U = 1, L = 2, N = 4, S = 8, P = 16, C = 32, X = 64, B = 128 };
    uint32_t ct = arena::static_alloc(257, 4);
    auto* t = hp<uint8_t>(ct);
    t[0] = 0;
    for (int c = 0; c < 256; ++c) {
        uint8_t f = 0;
        if (c < 32 || c == 127) f |= C;
        if (c >= 9 && c <= 13) f |= S;
        if (c == ' ') f |= S | B;
        if (c >= '0' && c <= '9') f |= N;
        if (c >= 'A' && c <= 'Z') f |= U;
        if (c >= 'a' && c <= 'z') f |= L;
        if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) f |= X;
        if (c > 32 && c < 127 && !(f & (U | L | N))) f |= P;
        t[1 + c] = f;
    }
    add_table_pointer("_ctype_", ct);
    uint32_t lo = arena::static_alloc(257 * 2, 4), up = arena::static_alloc(257 * 2, 4);
    auto* lt = hp<int16_t>(lo);
    auto* ut = hp<int16_t>(up);
    lt[0] = ut[0] = -1;
    for (int c = 0; c < 256; ++c) {
        lt[1 + c] = static_cast<int16_t>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        ut[1 + c] = static_cast<int16_t>(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    add_table_pointer("_tolower_tab_", lo);
    add_table_pointer("_toupper_tab_", up);
    uint32_t guard = arena::static_alloc(4, 4);
    *hp<uint32_t>(guard) = std::random_device{}() | 0x00FF0000u;
    hle::add_data("__stack_chk_guard", guard);
    uint32_t page = arena::static_alloc(4, 4);
    *hp<uint32_t>(page) = 4096;
    hle::add_data("__page_size", page);

    // Legacy BSD sys_siglist is a guest char*[] data symbol. Never expose
    // host pointers here: every entry must be a 32-bit address in the arena.
    constexpr uint32_t kSignalCount = 65;
    uint32_t siglist = arena::static_alloc(kSignalCount * sizeof(uint32_t), 4);
    auto* guest_signals = hp<uint32_t>(siglist);
    for (uint32_t i = 0; i < kSignalCount; ++i) {
        const char* description = strsignal(static_cast<int>(i));
        guest_signals[i] = arena::static_strdup(description ? description : "Unknown signal");
    }
    hle::add_data("sys_siglist", siglist);

    // Expose a guest-width environ variable with an empty, null-terminated
    // environment. Never hand the 64-bit host environ pointer to ARM32 code.
    uint32_t guest_env = arena::static_alloc(sizeof(uint32_t), 4);
    *hp<uint32_t>(guest_env) = 0;
    uint32_t environ_var = arena::static_alloc(sizeof(uint32_t), 4);
    *hp<uint32_t>(environ_var) = guest_env;
    hle::add_data("environ", environ_var);
}

const hle::Def kDefs[] = {
    H("malloc", g_malloc), H("memalign", g_memalign), H("calloc", g_calloc), H("realloc", g_realloc), H("free", g_free),
    HE("mmap", g_mmap), H("munmap", g_munmap),
    H("memcpy", g_memcpy), H("memmove", g_memmove), H("memset", g_memset), H("memcmp", g_memcmp),
    H("memchr", g_memchr), H("memrchr", g_memrchr), H("memmem", g_memmem), H("memccpy", g_memccpy),
    H("__aeabi_memcpy", g_memcpy), H("__aeabi_memmove", g_memmove), H("__aeabi_memset", g_aeabi_memset),
    H("strlen", g_strlen), H("strnlen", g_strnlen), H("strtok", g_strtok), H("isspace", g_isspace), H("isalnum", g_isalnum), H("strcmp", g_strcmp), H("strncmp", g_strncmp), H("strcasecmp", g_strcasecmp),
    H("strncasecmp", g_strncasecmp), H("strcoll", g_strcoll), H("strxfrm", g_strxfrm), H("strcpy", g_strcpy),
    H("strncpy", g_strncpy), H("strlcpy", g_strlcpy), H("strcat", g_strcat), H("strncat", g_strncat), H("strchr", g_strchr),
    H("strrchr", g_strrchr), H("strstr", g_strstr), H("strpbrk", g_strpbrk), H("strcspn", g_strcspn),
    H("strdup", g_strdup), H("strtok_r", g_strtok_r), H("strerror", g_strerror), H("strerror_r", g_strerror_r),
    H("basename", g_basename), H("fnmatch", g_fnmatch), HE("putenv", g_putenv), H("mktemp", g_mktemp),
    H("wcslen", g_wcslen), H("wmemchr", g_wmemchr), H("wmemcmp", g_wmemcmp), H("wmemcpy", g_wmemcpy),
    H("wmemmove", g_wmemmove), H("wmemset", g_wmemset), H("wcscoll", g_wcscoll), H("wcsxfrm", g_wcsxfrm),
    H("btowc", g_btowc), H("wctob", g_wctob), H("towlower", g_towlower), H("towupper", g_towupper),
    H("wctype", g_wctype), H("iswctype", g_iswctype), H("mbrtowc", g_mbrtowc), H("wcrtomb", g_wcrtomb),
    H("atoi", g_atoi), HE("strtol", g_strtol), HE("strtoul", g_strtoul), HE("strtoll", g_strtoll),
    HE("strtoull", g_strtoull), HE("strtod", g_strtod), HRAW("sscanf", g_sscanf),
    H("acos", m_acos), H("asin", m_asin), H("atan", m_atan), H("ceil", m_ceil), H("cos", m_cos), H("cosh", m_cosh),
    H("exp", m_exp), H("floor", m_floor), H("log", m_log), H("log10", m_log10), H("rint", m_rint), H("sin", m_sin),
    H("sinh", m_sinh), H("sqrt", m_sqrt), H("tan", m_tan), H("tanh", m_tanh), H("trunc", m_trunc),
    H("acosf", m_acosf), H("asinf", m_asinf), H("atanf", m_atanf), H("expf", m_expf), H("fabsf", m_fabsf), H("logf", m_logf), H("ceilf", m_ceilf), H("cosf", m_cosf), H("floorf", m_floorf), H("sinf", m_sinf), H("sqrtf", m_sqrtf),
    H("tanf", m_tanf), H("__isinf", m_isinf), H("lrint", m_lrint), H("atan2", m_atan2), H("fmod", m_fmod), H("pow", m_pow), H("atan2f", m_atan2f),
    H("fmodf", m_fmodf), H("powf", m_powf), H("ldexp", m_ldexp), H("frexp", m_frexp), H("modf", m_modf),
    H("isnan", m_isnan), H("lrand48", g_lrand48), H("srand48", g_srand48),
    H("time", g_time), HE("gettimeofday", g_gettimeofday), HE("clock_gettime", g_clock_gettime),
    HE("nanosleep", g_nanosleep), H("clock", g_clock), H("difftime", g_difftime), H("gmtime_r", g_gmtime_r),
    H("localtime_r", g_localtime_r), H("gmtime", g_gmtime), H("localtime", g_localtime), H("mktime", g_mktime),
    H("strftime", g_strftime), H("wcsftime", g_wcsftime), HE("utime", g_utime), HE("utimes", g_utimes),
    HE("open", g_open), HE("close", g_close), HE("read", g_read), HE("write", g_write), HE("lseek", g_lseek), HE("lseek64", g_lseek64),
    HE("fsync", g_fsync), HE("getrlimit", g_getrlimit), HE("setrlimit", g_setrlimit), HE("uname", g_uname), HE("flock", g_flock), HE("dup", g_dup), HE("dup2", g_dup2), HE("fchmod", g_fchmod), HE("fchown", g_fchown), HE("ftruncate", g_ftruncate), HE("chdir", g_chdir), HE("readlink", g_readlink), HE("gettid", g_gettid), HE("pipe", g_pipe), HE("poll", g_poll), HE("writev", g_writev), HE("access", g_access),
    HE("chmod", g_chmod), HE("mkdir", g_mkdir), HE("rmdir", g_rmdir), HE("unlink", g_unlink),
    HE("remove", g_remove), HE("rename", g_rename), HE("pathconf", g_pathconf), HE("getcwd", g_getcwd),
    HE("fcntl", g_fcntl), HE("ioctl", g_ioctl), HE("stat", g_stat), HE("fstat", g_fstat), HE("statfs", g_statfs),
    HE("opendir", g_opendir), HE("closedir", g_closedir), H("readdir_r", g_readdir_r), H("readdir", g_readdir),
    HE("getaddrinfo", g_getaddrinfo), H("freeaddrinfo", g_freeaddrinfo),
    H("gethostbyname", g_gethostbyname), H("gethostbyname2", g_gethostbyname2), H("gethostbyaddr", g_gethostbyaddr), H("getnameinfo", g_getnameinfo),
    H("getprotobyname", g_getprotobyname), H("getprotobynumber", g_getprotobynumber),
    HE("socket", g_socket), HE("socketpair", g_socketpair), HE("accept", g_accept), HE("listen", g_listen), HE("sendto", g_sendto), HE("shutdown", g_shutdown), HE("select", g_select), HE("bind", g_bind), HE("connect", g_connect), HE("getsockname", g_getsockname),
    HE("getpeername", g_getpeername), HE("send", g_send), HE("recv", g_recv), HE("recvfrom", g_recvfrom),
    HE("setsockopt", g_setsockopt), HE("getsockopt", g_getsockopt), H("inet_addr", g_inet_addr),
    H("inet_ntop", g_inet_ntop), H("inet_pton", g_inet_pton), HE("gethostname", g_gethostname),
    H("getpid", g_getpid), H("getuid", g_getuid), H("geteuid", g_geteuid), H("getgid", g_getgid), H("getegid", g_getegid), H("getpwuid", g_getpwuid), H("getenv", g_getenv),
    H("setlocale", g_setlocale), H("sysconf", g_sysconf), H("getauxval", g_getauxval),
    H("getrandom", g_getrandom), H("sysinfo", g_sysinfo), H("usleep", g_usleep), H("sleep", g_sleep), HE("sem_init", g_sem_init), HE("sem_destroy", g_sem_destroy), HE("sem_getvalue", g_sem_getvalue), HE("sem_post", g_sem_post), HE("sem_wait", g_sem_wait), HE("sem_timedwait", g_sem_timedwait), H("get_h_errno", g_get_h_errno), H("__get_h_errno", g_get_h_errno), HE("__mmap2", g_mmap2), HRAW("qsort", g_qsort), HRAW("bsearch", g_bsearch), HRAW("__android_log_print", g_android_log_print), HRAW("__android_log_assert", g_android_log_assert), H("__system_property_get", g_system_property_get), H("sched_yield", g_sched_yield),
    H("sigaction", g_sigaction), H("sigprocmask", g_sigprocmask), HRAW("abort", g_abort), H("exit", g_exit),
    H("raise", g_raise), HRAW("__stack_chk_fail", g_stack_chk_fail), H("__assert2", g_assert2), HRAW("syscall", g_syscall),
    H("dlopen", g_dlopen), H("dlsym", g_dlsym), H("dlclose", g_dlclose), H("dlerror", g_dlerror), H("dladdr", g_dladdr),
    H("__google_potentially_blocking_region_begin", g_google_potentially_blocking_region_begin),
    H("__google_potentially_blocking_region_end", g_google_potentially_blocking_region_end),
    H("__cxa_atexit", g_cxa_atexit), H("__cxa_finalize", g_cxa_finalize), H("__errno", g_errno),
    H("__gnu_Unwind_Find_exidx", g_unwind_find_exidx),
};

}  // namespace

void hle::register_libc() {
    make_data();
    add_defs(kDefs, std::size(kDefs));
}
