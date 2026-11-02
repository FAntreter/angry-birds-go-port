# Angry Birds Go! ARM64 bridge port — engineering work package

This is a source-level investigation package, not an installable APK and not a completed port.

## Inputs
- Reference bridge source: `bridge_src/` extracted from the uploaded Star Wars bridge ZIP.
- ARMv7 libraries: `target_libs/` extracted from `angry-birds-go-2-9-1.apk`.

## Verified native dependency findings
`libABK.so` declares these `DT_NEEDED` libraries:
- `libandroid.so`
- `libEGL.so`
- `libGLESv2.so`
- `libgnustl_shared.so`
- `libjnigraphics.so`
- `liblog.so`
- `libOpenSLES.so`
- `libSkynestSDK.so`
- `libstdc++.so`
- `libm.so`
- `libc.so`
- `libdl.so`

The supplied bridge's `bridge.cpp` hardcodes `libabsw_arm32.so` and only loads that guest game library after its embedded runtime. `elf32.cpp` does not process `DT_NEEDED`; symbol resolution checks exports from already-loaded modules and HLE handlers, then creates an unresolved thunk. `hle::init()` registers libc, stdio, pthread, GLES, and Android asset handlers. This means the existing bridge cannot simply be pointed at `libABK.so` and expected to load its dependency graph.

## Safe next implementation milestones
1. Add a dependency manifest/parser for `DT_NEEDED` and symbol imports.
2. Load guest modules in dependency order, ensuring guest libraries are distinguished from host Android libraries that need HLE/forwarding.
3. Add EGL API forwarding compatible with guest pointers and native-window lifecycle.
4. Implement/forward `libjnigraphics` imports as needed.
5. Add an OpenSL ES compatibility path or intentionally disabled audio path for diagnostic boot only.
6. Instrument dynamic JNI registration (`RegisterNatives`) and compare actual descriptors.
7. Build on an Android NDK-equipped environment, then test on the target device.

## Important limitation
No APK is built here. The source bridge is an architecture reference, not a drop-in Go port. This package intentionally does not include the original game's full assets or the reference APKs.


## Stage 3: DT_NEEDED parser

The ELF loader now has a bounded `elf::needed()` parser. Validation results and limitations are in `reports/stage3_dt_needed_parser.md`; `tools/validate_dt_needed.py` compares parsed dependencies against `readelf`. This is not yet recursive loader integration and does not produce a playable APK.

## Stage 4 — dependency plan

Added `tools/plan_guest_load_order.py`. It reads `DT_NEEDED` from the extracted ARM32 shared objects using `readelf`, classifies dependencies into extracted guest libraries vs Android/HLE/forwarding libraries, and emits a dependency-first order. Current root plan for `libABK.so` is `libgnustl_shared.so`, `libSkynestSDK.so`, then `libABK.so`. This is static planning only; the bridge startup code is not yet recursively loading these modules, and host forwarding coverage is not implied.

## Stage 5 — HLE symbol coverage audit

Added `tools/audit_hle_symbol_coverage.py` and `reports/stage5_hle_coverage.md`. The script compares undefined dynamic symbol names from each guest ELF with names explicitly registered in the bridge HLE tables. This is a conservative static name comparison only: unmatched names may be provided by another guest library, exported as data, or handled through a mechanism the simple source scan does not recognize. It is not a definitive runtime missing-symbol list.

The `libABK.so` report highlights several concrete gaps to prioritize: EGL entry points, OpenSL ES entry points/constants, `ANativeWindow_setBuffersGeometry`, and some Android asset-manager calls. It also lists many `rcs::*` C++ symbols that may resolve from `libSkynestSDK.so` when cross-module relocation is implemented. The report must not be read as meaning every unmatched symbol needs a new HLE implementation.

## Stage 6 — Android asset-manager HLE additions

Added source wrappers and HLE registrations for `AAssetManager_openDir`, `AAssetDir_getNextFileName`, `AAssetDir_rewind`, `AAssetDir_close`, and `AAsset_openFileDescriptor`. A static regression check is available at `tools/validate_asset_hle.py`; details and ABI caveats are in `reports/stage6_android_asset_hle.md`. These additions are not yet NDK-compiled or device-tested, and they do not resolve the remaining graphics/audio/loader work.

## Stage 7 — recursive dependency-loader integration

`bridge.cpp` now has a dependency-first recursive loader for the Go `libABK.so` guest ELF, using the existing `elf::needed()` parser. ARM32 guest copies of bundled dependencies are expected as `_arm32.so` files alongside the host bridge; Android system dependencies are explicitly classified as host/HLE. The loader runs constructors dependency-first and rejects cycles rather than hanging. See `reports/stage7_recursive_loader.md` and run `tools/validate_recursive_loader.py`. This remains a first-pass loader and is not NDK-compiled or device-tested.

## Stage 9 — guest import-resolution audit

Added `tools/audit_guest_import_resolution.py` and `reports/stage9_guest_import_resolution.md`. This compares undefined ARM32 dynamic symbol names against bundled guest-library exports and HLE registration names. It highlights unresolved or unclassified imports that must be addressed before a diagnostic build can be trusted. The report is deliberately conservative: name matching does not prove ABI correctness, and it does not fully model ELF symbol lookup scopes or runtime `dlsym` paths. This stage adds diagnostics only; it does not implement EGL, OpenSL ES, or make the project buildable/playable.

Run: `python3 tools/audit_guest_import_resolution.py`

## Stage 10 — additional GLES2 HLE wrappers

Added 18 straightforward GLES2 wrappers and registrations that were missing from the initial HLE table, including separate blend modes, buffer sub-data, mipmap generation, stencil operations, and float uniform arrays. `tools/validate_gles_hle.py` checks that each wrapper and registration remains present. This is a partial graphics improvement only: EGL and Android native-window lifecycle integration remain unimplemented, and the source has not been NDK-compiled or device-tested.

Run: `python3 tools/validate_gles_hle.py`

## Stage 11 — relocation compatibility and unresolved-import diagnostics

Stage 11 improves loader diagnostics: unresolved imports now report the importing guest library and whether the import is weak or strong, and the message explicitly identifies the installed unresolved-symbol thunk as diagnostic fallback rather than a real implementation. Added `tools/audit_elf_relocations.py` and `reports/stage11_relocation_compatibility.md` to compare the relocation types present in bundled guest ELFs with the loader's currently supported set. Static audit found four relocation types and no unsupported relocation entries in the current bundled libraries. This does not prove loader correctness or successful execution; EGL/native-window and OpenSL ES remain major runtime gaps, and no Android NDK build/device test has been performed.

## Stage 12 — guest payload staging

Added `tools/stage_guest_libs.py` to validate ARM32 ELF identity and copy the three required guest libraries to the `_arm32.so` filenames expected by the recursive loader. This catches a practical mismatch between source filenames and runtime expectations. See `reports/stage12_guest_payload_staging.md`. This only stages guest native payloads; it is not an APK build. EGL/native-window and OpenSL ES support remain unimplemented, and no Android NDK build/device test has been completed.

## Stage 13 — recursive dependency packaging audit

Added `tools/audit_dependency_packaging.py`, which follows `DT_NEEDED` from `libABK.so`, checks the current bridge host-library allowlist, verifies that each reachable guest dependency has a bundled ARM32 ELF source, and checks that the bridge contains a guest filename mapping. Current closure is three guest libraries (`libgnustl_shared.so`, `libSkynestSDK.so`, and `libABK.so`) with no missing guest payloads in the static audit. Report: `reports/stage13_dependency_packaging.md`.

Run: `python3 tools/audit_dependency_packaging.py`

This closes a packaging validation gap only. It does not solve imported-symbol resolution or EGL/native-window/OpenSL ES support, and does not build or test an APK.

## Stage 14 — restore generated build inputs and add Android CMake target

The source tree was missing three generated include files referenced by C++ sources. Restored `guest_rt_blob.inc` and `kuser_blob.inc` from the reference source, and regenerated `natives_gen.inc` from the Go APK's 120 DEX native declarations so the bridge no longer reuses the Star Wars native table. Restored `gheap.c`, which is required by the arena allocator. Added a root `CMakeLists.txt` for an Android `arm64-v8a` shared-library target and a build-input validator. See `reports/stage14_build_inputs.md`.

Validation: `python3 tools/validate_build_inputs.py` passes. This environment lacks the Android NDK and Dynarmic source checkout, so no Android compile has been performed. EGL/native-window and OpenSL ES are still unresolved, and there is still no playable APK.

## Stage 15 — reproducible Android build preflight

Added `tools/preflight_android_build.py` to check for the Android NDK, a Dynarmic A32 source checkout, CMake/Ninja, and required bridge inputs. The first actual CMake configure attempt in this environment was blocked by the explicit `if(NOT ANDROID)` guard because no Android NDK toolchain is installed. No compile or APK build occurred. See `reports/stage15_build_preflight.md`.

Run: `python3 tools/preflight_android_build.py`

## Stage 16 — initial EGL/native-window HLE boundary

Added `bridge_src/hle_egl.cpp` with tokenized guest handles for EGLDisplay, EGLConfig, EGLContext, EGLSurface, and ANativeWindow. This avoids truncating 64-bit host pointer handles into ARM32 guest values. The first implementation covers the imported EGL lifecycle/configuration/surface calls, `eglGetProcAddress` lookup through the HLE registry, and Android `ANativeWindow_fromSurface`, `ANativeWindow_release`, and `ANativeWindow_setBuffersGeometry`. Added the file to CMake and registered it during HLE initialization. A static regression test checks the registration/build wiring.

Validation: `tools/validate_egl_hle.py`, build-input, asset, recursive-loader, and GLES static checks pass. This is a first implementation, not a verified graphics path: no Android NDK build or on-device test was possible here. Remaining concerns include ABI correctness for every imported call, guest `eglGetProcAddress` fallback behavior for symbols not registered in HLE, Android surface lifecycle interactions, and OpenSL ES audio. Do not treat this as a playable APK.

## Stage 17 — EGL lifecycle hardening

The EGL termination wrapper now checks for an invalid display and clears stale display/config/context/surface token mappings after successful `eglTerminate`. This prevents reuse of host EGL handles that the host has invalidated. Run `python3 tools/validate_egl_lifecycle.py` to check the source invariant. This remains a source-level change; it is not a compiled or device-tested graphics implementation.

## Stage 18 — EGL per-display ownership

Stage 18 replaces global EGL config/context/surface token cleanup with per-display ownership tracking. Run `python3 tools/validate_egl_lifecycle.py` and review `reports/stage18_egl_per_display_ownership.md` plus `reports/stage18_validation.txt`. This is static validation only; the Android NDK build and on-device testing remain outstanding.

## Stage 19 — EGL token isolation across displays

Adjusted EGL token mapping so configs, contexts, and surfaces are associated with their owning display during insertion, and token reuse requires the same host handle and the same display owner. Destruction also clears owner metadata. This prevents a shared host-handle value from silently changing which display owns an existing guest token. Static validators pass; this has not been Android-NDK-compiled or device-tested. See `reports/stage19_egl_token_isolation.md`.

## Stage 20 — collision-safe tokens and EGL ownership enforcement

The EGL/native-window bridge now allocates guest tokens through a collision-checking allocator and rejects stale or cross-display config/context/surface tokens at resource-operation boundaries. New static regression checks are `tools/validate_egl_token_allocator.py` and `tools/validate_egl_token_ownership.py`. These checks pass in the current source package, alongside the prior static validators. This remains an uncompiled source package: Android NDK and Dynarmic inputs are not available in the build environment, so no APK or runtime claim is made.

## Stage 20 — collision-safe tokens and EGL ownership enforcement

The EGL/native-window bridge now allocates guest tokens through a collision-checking allocator and rejects stale or cross-display config/context/surface tokens at resource-operation boundaries. New static regression checks are `tools/validate_egl_token_allocator.py` and `tools/validate_egl_token_ownership.py`. These checks pass in the current source package, alongside the prior static validators. This remains an uncompiled source package: Android NDK and Dynarmic inputs are not available in the build environment, so no APK or runtime claim is made.

## Stage 21 — consolidated release-readiness assessment

`reports/stage21_release_readiness.md` consolidates current validation results and identifies the real remaining blockers. Static checks pass for build inputs, recursive loader structure, EGL token/lifecycle guards, GLES wrapper registrations, asset HLE, dependency packaging, and the current relocation inventory. The graphics/audio audit still finds 9 OpenSL ES-related imports without direct HLE registration, and the guest import audit reports 335 unique names without a matching guest export or HLE name. Those are static name-based findings, not all necessarily runtime failures, but they require resolution.

**No Android NDK or Dynarmic A32 checkout is available here, so no Android compile or APK build has occurred. This remains source code, not a playable APK.**

## Stage 22 — OpenSL ES compatibility (silent diagnostic shim)

Added a first-pass guest-ABI OpenSL ES layer in `bridge_src/hle_opensles.cpp`. It registers the game's imported `slCreateEngine` and seven `SL_IID_*` data symbols, plus guest-address-space vtables for object, engine, play, Android simple-buffer-queue, and volume interfaces. Basic calls are handled without exposing 64-bit host interface pointers to the 32-bit guest. This shim intentionally does not produce sound or translate guest callbacks, and it is not yet verified by NDK compilation or device testing. Run `python3 tools/validate_opensles_hle.py` for static guardrails.

## Stage 24 — OpenSL ES lifecycle and queue hardening

The audio shim now protects mutable per-object state with a mutex, validates queued guest buffer ranges, and invalidates destroyed objects while clearing queue and callback bookkeeping. See `reports/stage24_audio_lifecycle.md` and run `python3 tools/validate_opensles_lifecycle.py`. This remains a silent compatibility layer: real audio output and guest callback execution are not implemented. The Android NDK and Dynarmic A32 checkout are still unavailable, so this source has not been compiled and no APK is available.
