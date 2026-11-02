# Stage 9 — guest import resolution audit

Generated from `readelf --dyn-syms` for bundled ARM32 libraries and HLE registration names found in bridge source.

**Important:** a name match is not proof of ABI correctness or runtime resolution. This audit does not account for symbol versions, visibility, exact ELF lookup scopes, or symbols obtained dynamically.

## Summary

- Undefined symbol names inspected: **1183** (counted per library/import occurrence).
- HLE-name matches: **481**.
- Names exported by another bundled guest ELF: **368**.
- Names with no matching guest export/HLE name: **334**.

## Unmatched imports by library

### `libABK.so` — 163 unmatched names
`SL_IID_ANDROIDSIMPLEBUFFERQUEUE`, `SL_IID_ENGINE`, `SL_IID_ENGINECAPABILITIES`, `SL_IID_PLAY`, `SL_IID_PLAYBACKRATE`, `SL_IID_SEEK`, `SL_IID_VOLUME`, `__aeabi_memset`, `__android_log_assert`, `__get_h_errno`, `__isinf`, `__mmap2`, `__page_size`, `__sF`, `_ctype_`, `_exit`, `_tolower_tab_`, `_toupper_tab_`, `abort`, `accept`, `access`, `acosf`, `asinf`, `atanf`, `bind`, `bsd_signal`, `bsearch`, `chdir`, `chmod`, `clock_gettime`, `close`, `closedir`, `connect`, `dup2`, `environ`, `execv`, `execve`, `execvp`, `expf`, `fabsf`, `fchmod`, `fchown`, `fclose`, `fcntl`, `fdopen`, `fflush`, `fgetc`, `fgets`, `flock`, `fopen`, `fork`, `fprintf`, `fputc`, `fputs`, `fread`, `freeaddrinfo`, `freopen`, `fseek`, `fstat`, `fsync`, `ftell`, `ftruncate`, `fwrite`, `getaddrinfo`, `getc`, `getcwd`, `getegid`, `getgid`, `gethostbyaddr`, `gethostbyname`, `gethostbyname2`, `gethostname`, `getnameinfo`, `getpeername`, `getprotobyname`, `getprotobynumber`, `getrlimit`, `getsockname`, `getsockopt`, `gettimeofday`, `getuid`, `ioctl`, `kill`, `listen`, `logf`, `longjmp`, `lrint`, `lseek`, `lseek64`, `memalign`, `memccpy`, `mkdir`, `mktemp`, `mmap`, `mremap`, `msync`, `nanosleep`, `open`, `opendir`, `pipe`, `poll`, `printf`, `pthread_attr_destroy`, `pthread_attr_init`, `pthread_attr_setdetachstate`, `pthread_attr_setstacksize`, `pthread_exit`, `pthread_kill`, `pthread_once`, `putchar`, `putenv`, `puts`, `qsort`, `read`, `readdir`, `readlink`, `recv`, `recvfrom`, `remove`, `rename`, `rmdir`, `select`, `sem_destroy`, `sem_getvalue`, `sem_init`, `sem_post`, `sem_timedwait`, `sem_wait`, `send`, `sendfile`, `sendto`, `setjmp`, `setrlimit`, `setsockopt`, `shutdown`, `sigwait`, `sleep`, `snprintf`, `socket`, `socketpair`, `sprintf`, `sscanf`, `stat`, `strnlen`, `strtod`, `strtok`, `strtol`, `strtoll`, `strtoul`, `syscall`, `sysinfo`, `syslog`, `uname`, `ungetc`, `unlink`, `usleep`, `utimes`, `vfprintf`, `vsnprintf`, `vsprintf`, `waitpid`, `write`, `writev`

### `libSkynestSDK.so` — 76 unmatched names
`__android_log_print`, `__sF`, `__stack_chk_fail`, `__stack_chk_guard`, `_ctype_`, `_tolower_tab_`, `_toupper_tab_`, `abort`, `access`, `bind`, `bsearch`, `chmod`, `clock_gettime`, `close`, `closedir`, `connect`, `dup`, `fclose`, `fcntl`, `fflush`, `fgets`, `fopen`, `fprintf`, `fputc`, `fputs`, `fread`, `fseek`, `fstat`, `fsync`, `ftell`, `fwrite`, `getcwd`, `gethostname`, `getpeername`, `getsockname`, `getsockopt`, `gettimeofday`, `ioctl`, `isspace`, `lseek`, `mkdir`, `mmap`, `nanosleep`, `open`, `opendir`, `pathconf`, `pipe`, `poll`, `printf`, `pthread_once`, `qsort`, `read`, `recv`, `recvfrom`, `rename`, `rmdir`, `send`, `setsockopt`, `siglongjmp`, `sigsetjmp`, `snprintf`, `socket`, `sprintf`, `sscanf`, `stat`, `statfs`, `strtod`, `strtol`, `strtoll`, `strtoul`, `strtoull`, `unlink`, `utime`, `vsnprintf`, `write`, `writev`

### `libadcolony.so` — 24 unmatched names
`__aeabi_memset`, `__android_log_print`, `__sF`, `__stack_chk_fail`, `__stack_chk_guard`, `abort`, `close`, `dladdr`, `fprintf`, `fputc`, `gettid`, `open`, `printf`, `pthread_once`, `putchar`, `puts`, `qsort`, `read`, `sprintf`, `strlcpy`, `strtol`, `sys_siglist`, `vfprintf`, `write`

### `libgnustl_shared.so` — 41 unmatched names
`__assert2`, `__google_potentially_blocking_region_begin`, `__google_potentially_blocking_region_end`, `__sF`, `_ctype_`, `abort`, `clock_gettime`, `fclose`, `fdopen`, `fflush`, `fopen`, `fputc`, `fputs`, `fread`, `fseek`, `fstat`, `ftell`, `fwrite`, `getc`, `gettimeofday`, `getwc`, `ioctl`, `isalnum`, `isspace`, `lseek`, `nanosleep`, `poll`, `pthread_once`, `putc`, `putwc`, `read`, `sprintf`, `sscanf`, `strtod`, `strtoul`, `syscall`, `ungetc`, `ungetwc`, `vsprintf`, `write`, `writev`

### `libjs.so` — 30 unmatched names
`__aeabi_memset`, `__page_size`, `__sF`, `__stack_chk_fail`, `__stack_chk_guard`, `abort`, `close`, `fputc`, `fputs`, `fwrite`, `gettimeofday`, `mmap`, `open`, `pthread_attr_destroy`, `pthread_attr_getstack`, `pthread_attr_init`, `pthread_getattr_np`, `pthread_once`, `puts`, `qsort`, `read`, `setjmp`, `snprintf`, `sprintf`, `strtol`, `syscall`, `usleep`, `vfprintf`, `vsnprintf`, `write`

## Interpretation and next actions

1. Prioritize EGL and `ANativeWindow_setBuffersGeometry`: they are required for a real graphics path and cannot be replaced by arbitrary no-op thunks.
2. Resolve OpenSL ES IID data symbols and interface/vtable calls together; a function-name-only implementation is insufficient.
3. Audit C++ imports against the exact dependency export table and ELF symbol-version/visibility data before assuming `libSkynestSDK.so` will satisfy them.
4. Add runtime diagnostics that report the importing module and relocation type for every unresolved import before constructors run.
5. Compile with the Android NDK and validate the loader on device before estimating a playable milestone more confidently.

## Re-run
`python3 tools/audit_guest_import_resolution.py`
