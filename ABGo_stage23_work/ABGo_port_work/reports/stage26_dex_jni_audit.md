# Stage 26 — Angry Birds Go! 2.9.1 DEX/JNI audit

## Input APK fingerprint

Rechecked the uploaded original APK directly.

- File: `angry-birds-go-2-9-1.apk`
- Size: 86,643,025 bytes
- SHA-256: `b7aa722b63cef19dd365d57291e0580316253b969768c3f1b1e687d7fd60fab8`
- ZIP entries: 2,032
- DEX: `classes.dex` (8,972,972 bytes), `classes2.dex` (2,155,380 bytes)

The fingerprint matches the previously recovered APK audit.

## DEX native declarations

A direct DEX table walk found **120 native method declarations in classes.dex** and none in classes2.dex. They span 34 Java classes.

The repository's generated `bridge_src/natives_gen.inc` contains 120 trampoline entries, so its inventory count agrees with the uploaded APK.

The most important gameplay-facing class is `com.exient.ExientGLSurfaceView`, with 17 native methods:

- Context and surface lifecycle: `nativeContextCreated`, `nativeContextDestroyed`, `nativeSurfaceCreated`, `nativeSurfaceChanged`, `nativeWindowSurfaceCreated`, `nativeWindowSurfaceDestroyed`
- Rendering: `nativeDrawFrame`
- Initialization and shutdown: `nativeInitialize`, `nativeTerminate`, `nativeStop`
- Lifecycle/focus: `nativePause`, `nativeResume`, `nativeWindowFocusChanged`
- Input: `nativeInputAddTouchEvent`, `nativeInputCancelTouchEvents`, `nativeGamepadAddKeyEvent`, `nativeGamepadAddMotionEvent`

This confirms that a functioning Java Activity and GL-surface lifecycle are essential; loading the shared library alone is not enough.

Other native declarations are spread across Exient XGS, Rovio's Activity/Chromecast integrations, Fusion wrappers, the Rovio Client Services SDK, and AdColony.

## Static JNI export cross-check

Compared each DEX native declaration's conventional JNI export name against the ARMv7 libraries' dynamic symbol tables:

| Library | DEX declarations found by conventional exported name |
|---|---:|
| `libABK.so` | 33 |
| `libSkynestSDK.so` | 42 |
| `libadcolony.so` | 13 |
| **Total** | **88 / 120** |

The other 32 declarations do not appear as conventional exported `Java_...` symbols in this static scan. This is **not yet proof that they are missing**: they may be supplied through `RegisterNatives`, be optional integration callbacks, or need further native call-path analysis. The bridge already has a `RegisterNatives` interception path, and the SDK's `JNI_OnLoad` is now invoked during guest startup. The next validation must establish which of these 32 are actually registered and whether every registration is matched to a trampoline.

## Native dependency / HLE cross-check

The main `libABK.so` DT_NEEDED list includes Android assets, EGL, GLES2, OpenSL ES, native-window support, pthread/libc, and the GNU C++ runtime.

The current repository has guest-ABI HLE layers for:
- Android asset-manager functions in `hle_android.cpp`
- EGL and ANativeWindow handle translation in `hle_egl.cpp`
- GLES2 imports in `hle_gles.cpp`
- OpenSL ES object/interface/queue bookkeeping in `hle_opensles.cpp`

The OpenSL ES implementation is still explicitly silent/no-op playback. Queue state and lifecycle bookkeeping do not mean that audio is audible or callbacks are fully emulated.

Although `libjnigraphics.so` is listed as a dependency of `libABK.so`, this scan found no `AndroidBitmap_...` imports in the main engine's dynamic symbol table. Keep the dependency available, but do not prioritize bitmap HLE without a concrete imported symbol or runtime trace.

## Next engineering work

1. Trace the SDK's guest `JNI_OnLoad` and every guest `RegisterNatives` call. Record which of the 32 non-conventional declarations are registered, and fail loudly for unmatched registrations instead of silently skipping them.
2. Validate all 120 trampoline descriptors against the DEX class/method/signature inventory, including static-vs-instance calling convention.
3. Audit the Java startup path around `ExientGLSurfaceView`, Activity lifecycle, and `AssetManager` handoff.
4. Replace silent OpenSL ES playback with an ABI-safe audio backend and real queue completion/callback behavior.
5. Build a Java/Kotlin Android wrapper that owns the Activity, GL surface, asset manager, and bridge library load order; then package and test on an ARM64 device.

## Status and limitations

The ARM64 bridge CI build succeeded previously and stages the ARM32 guest libraries. This audit does **not** establish successful installation, startup, rendering, sound, or gameplay. No installable APK has been produced yet.
