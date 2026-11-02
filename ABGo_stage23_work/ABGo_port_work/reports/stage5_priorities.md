# Stage 5 — practical next priorities

Based on `stage5_hle_coverage.md`, the next source changes should be ordered as follows:

1. **Guest-module resolution first:** symbols with `rcs::*` prefixes are C++ SDK methods and should resolve from the guest `libSkynestSDK.so` where exported. Do not create fake HLE stubs for them.
2. **EGL forwarding:** the bridge has GLES HLE registrations but no EGL registration set in `hle::init()`. Implement an EGL layer with correct translation of guest `EGLDisplay`, `EGLConfig`, `EGLContext`, `EGLSurface`, and native-window lifecycle.
3. **OpenSL ES:** imports include `slCreateEngine` and OpenSL interface identifiers. Implement a diagnostic audio backend or a carefully bounded fallback only after identifying the engine's expected interfaces and callbacks.
4. **Android asset API completeness:** imports such as `AAssetManager_openDir`, `AAssetDir_getNextFileName`, `AAssetDir_close`, and `AAsset_openFileDescriptor` are not listed in the bridge's current HLE table and may be needed by content loading.
5. **Native window:** `ANativeWindow_setBuffersGeometry` needs a host-side forwarding/compatibility implementation if the Java activity supplies a native window.
6. **Re-audit after every change:** compare undefined symbols and run the bridge under trace to distinguish truly missing imports from unused/optional paths.

## Status

The static audit script and report are added. This stage does not add EGL/OpenSL implementations, does not compile the Android project, and does not establish that the game can launch.
