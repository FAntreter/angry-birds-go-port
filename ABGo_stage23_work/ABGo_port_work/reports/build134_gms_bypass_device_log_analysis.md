# Build 134 GMS-bypass device log analysis

Date: 2026-11-07

## Result

The uploaded `abgo_bridge_crash.txt` is a new run of the build-134 Google Play Services bypass diagnostic APK. The bypass did advance the Android startup sequence beyond the point where the earlier log stopped.

Evidence from the captured session:

- The guest `isDeviceAmazon()` callback returned `0x0000000500000001`; its ARM32 boolean return is R0 = 1.
- The next `isAdXTrackingOn()` callback ran and returned R0 = 0.
- The Exient GL surface lifecycle proceeded:
  - `nativeResume` returned.
  - `nativeContextCreated` returned.
  - `nativeWindowSurfaceCreated` returned.
  - `nativeInitialize` returned R0 = 1.
  - `nativeSurfaceCreated` returned.
  - `nativeSurfaceChanged` returned R0 = 0x500 (1280).
- `nativeDrawFrame()` was entered (guest function address `0x01bdb340`) but no matching return appears before the log ends.

This proves that the prior GMS startup branch was a blocker to reaching the graphics lifecycle, and that the bypass now reaches the first draw callback. It does not prove the game renders successfully.

## EGL findings

The log repeatedly records:

```
EGL getDisplay native=0x0 token=0x70000000 error=0x3000
```

`0x3000` is `EGL_SUCCESS`. The HLE wrapper maps the host EGL display to a guest token, so the nonzero token is expected and does not expose a host pointer to ARM32 code.

However, the capture contains no `EGL initialize`, `EGL createWindowSurface`, `EGL makeCurrent`, or `EGL swapBuffers` trace. The current `g_eglInitialize` wrapper already logs its result, as do the surface-creation, make-current, and swap wrappers. Their absence means the trace does not yet demonstrate that an EGL context/surface was initialized or that a frame was presented.

## Where execution stops

The final recorded native entry is `ExientGLSurfaceView.nativeDrawFrame`. After entry, the bridge logs reads from `/dev/urandom`, a socket returning `EAGAIN`, and pipe descriptors; the file ends before a JNI return or a fatal-signal report. Therefore classify this as an incomplete/stalled first-frame trace, not a confirmed SIGSEGV or other native crash.

The earlier ELF load still reports diagnostic unresolved strong imports including `mremap`, `pthread_exit`, `bsd_signal`, `_exit`, `msync`, `sigwait`, `pthread_kill`, `fork`, `waitpid`, `execve`, `execv`, `kill`, and `execvp`. These remain compatibility debt, but this log does not establish that any one of them caused the draw callback to stop.

## Next engineering step

Do not change the `isDeviceAmazon` or `isAdXTrackingOn` native return stubs based on this trace; both behaved as intended for this diagnostic. Preserve the current EGL handle-token mapping until a failing host EGL call is actually observed.

For the next diagnostic build, instrument the guest execution boundary around `nativeDrawFrame` with a bounded instruction/basic-block trace and explicit entry/exit timing. Also add an EGL call-sequence counter (including `eglInitialize`, `eglChooseConfig`, `eglCreateContext`, `eglCreateWindowSurface`, `eglMakeCurrent`, and `eglSwapBuffers`) and log each call's arguments/result/error. This will distinguish a guest CPU loop/block inside the draw callback from an EGL HLE path that is never reached or that fails before context creation.
