# Angry Birds Go! 2.9.1 — recovered APK audit

## Provenance and fingerprint

The original APK was uploaded on 2026-10-29 and its static audit was preserved in the working files.

- Filename: `angry-birds-go-2-9-1.apk`
- Size: 86,643,025 bytes
- SHA-256: `b7aa722b63cef19dd365d57291e0580316253b969768c3f1b1e687d7fd60fab8`
- ZIP entries: 2,032
- DEX files: `classes.dex`, `classes2.dex`

This repository does not currently contain the original APK bytes. This report records the recovered audit; it is not a replacement for the APK.

## ABI and engine

The APK contains five native libraries for each of `armeabi-v7a` and `x86`, and no `arm64-v8a` directory.

The primary ARMv7 engine is `libABK.so` (18,606,605 bytes). Its dependencies include:

- Android: `libandroid.so`, `libjnigraphics.so`, `liblog.so`
- Graphics: `libEGL.so`, `libGLESv2.so`
- Audio: `libOpenSLES.so`
- Runtime / game dependencies: `libgnustl_shared.so`, `libSkynestSDK.so`, `libstdc++.so`, `libm.so`, `libc.so`, `libdl.so`

Other ARMv7 libraries:

| Library | Size (bytes) | Notes |
|---|---:|---|
| `libSkynestSDK.so` | 5,717,728 | Substantial SDK; exports JNI entry points |
| `libgnustl_shared.so` | 5,630,676 | GNU C++ runtime |
| `libjs.so` | 935,488 | JavaScript runtime dependency |
| `libadcolony.so` | 38,244 | Advertising integration; optionality unproven |

Both `libABK.so` and `libSkynestSDK.so` export JNI symbols. The engine exports 42 `Java_*` JNI names; SkynestSDK exports 42. These counts do not include dynamically registered methods and do not establish that a host-side JNI adapter is compatible.

## Important engineering implications

1. This is a separate Go engine target. A bridge built for another Rovio game is reference code only; it is not a compatible drop-in.
2. The existing project stages the correct five ARM32 guest libraries, but the bridge still needs Go-specific startup/JNI validation.
3. The engine requires GLES2/EGL, Android asset-manager and bitmap APIs, and OpenSL ES. The OpenSL ES path is particularly important for lifecycle and callback correctness.
4. `libSkynestSDK.so` must be loaded and its imports resolved unless a verified startup trace proves it is optional.
5. `libadcolony.so` and `libjs.so` may be ancillary, but do not remove or stub them until Java and native call paths demonstrate that core gameplay does not need them.
6. A symbol-name scan is not an exact JNI signature audit. Exact class/method/descriptors and `RegisterNatives` paths still need DEX parsing and native disassembly/call-path review.

## Recovered JNI / API evidence

The main engine exports methods for `com.exient.ExientGLSurfaceView`, including context-created/context-destroyed, frame drawing, initialization, gamepad input and touch input. This strongly indicates that the original Java Activity / GL surface lifecycle is part of the engine startup contract.

The native import inventory includes:

- Android asset manager: `AAssetManager_fromJava`, `AAssetManager_open`, `AAssetManager_openDir`, `AAsset_getBuffer`, `AAsset_getLength`, and related functions.
- EGL: display/config/context/window-surface lifecycle and swap functions.
- GLES2: shader/program, texture, buffer, framebuffer, uniform and draw calls.
- OpenSL ES: `slCreateEngine`.
- pthread, libc, C++ exception/runtime and dynamic-loader functions.

## Next work order

1. Parse the original DEX files with a real DEX parser and extract every native declaration, class descriptor, method name and JNI signature.
2. Compare those declarations against `libABK.so` and `libSkynestSDK.so` exports and identify dynamic `RegisterNatives` calls.
3. Compare Go's required imports with the current HLE resolver and produce a Go-specific missing-symbol report.
4. Trace the Java startup path for `ExientGLSurfaceView`, Activity lifecycle, asset-manager handoff and native initialization.
5. Only then adapt the bridge and create an Android wrapper. Do not package a nonfunctional launcher as a game APK.

## Status

The existing ARM64 bridge build and ARM32 guest-library bundle are useful foundations. This recovered audit does **not** establish a working Go bridge, successful installation, or gameplay. The original APK bytes or a legally obtained extracted DEX/assets bundle are still needed for reproducible deeper analysis.
