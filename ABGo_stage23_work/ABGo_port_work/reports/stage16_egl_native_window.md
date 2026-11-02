# Stage 16 — EGL/native-window HLE boundary

## Implemented
- Added `hle_egl.cpp` and wired `register_egl()` into HLE initialization and CMake.
- Added wrappers for 21 EGL entry points, including display initialization, config selection, context/surface lifecycle, make-current, swapping, query calls, and proc-address lookup.
- Added wrappers for `ANativeWindow_fromSurface`, `ANativeWindow_release`, and `ANativeWindow_setBuffersGeometry`.
- Host EGL/native-window handles are held in host-side maps and exposed to the 32-bit guest as opaque 32-bit tokens rather than truncated host pointers.
- `ANativeWindow_fromSurface` uses a unique token per acquired reference so a matching guest release balances that reference.

## Static validation
`tools/validate_egl_hle.py` passes and confirms 24 registrations and CMake/HLE wiring. Existing build-input, Android asset, recursive-loader, and GLES checks pass.

## Not yet verified
This source has not been compiled with the Android NDK or exercised on a device. Static registration checks do not validate all EGL ABI details, surface lifecycle behavior, or successful rendering. `eglGetProcAddress` only returns registered HLE thunks and currently returns null for unknown names. OpenSL ES audio and APK packaging/testing remain unresolved. No playable APK is produced by this stage.
