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

## Stage 27 — long-form JNI export resolution and verified ARM64 rebuild

The bridge export binder now tries both JNI symbol forms for each generated native slot:

1. The short exported form already recorded in `NativeSlot::mangled`.
2. The JNI long form, formed by appending `__` and the encoded parameter descriptor.

This addresses APK libraries that expose long-form JNI names even when the DEX inventory contains only one matching method name. The static symbol cross-check now finds 8 additional methods through the long form, increasing the conventional export-name matches from 88/120 to 96/120. The remaining 24 still require investigation; this scan alone cannot determine whether they are dynamically registered, optional, or absent.

Commit: `45a56f42b088c636c42f2b8c78e14e72641dda2c`  
Build run: https://github.com/FAntreter/angry-birds-go-port/actions/runs/41506969867

Verified CI result:
- All workflow steps completed successfully, including static validators, Android ARM64 bridge compilation, guest-library staging, and artifact upload.
- Artifact: `AngryBirdsGoBridge-arm64-v8a`
- Artifact ID: `13170269781`
- Size: 21,855,942 bytes
- SHA-256: `381129dd4be261e21caf4acd246c13c690bab12ee38ac3725dc5a8f0b9a9b4ca`
- Artifact link: https://github.com/FAntreter/angry-birds-go-port/actions/runs/41506969867/artifacts/13170269781

The artifact is still a bridge/guest shared-library bundle, not an installable APK. This CI success is not an Android runtime or device test.

## Next gate

Before attempting device packaging, the next high-value checks are:
1. Produce a descriptor-by-descriptor comparison of all 120 DEX native declarations against `natives_gen.inc`, including static/instance flags and return/argument ABI types.
2. Capture and verify the SDK's actual `JNI_OnLoad` / `RegisterNatives` registrations. The bridge rejects any registration batch containing a method without a generated slot, but no runtime trace yet establishes which of the remaining 24 are registered.
3. Build the Android Java/Activity wrapper and package the host ARM64 bridge plus ARM32 guest libraries alongside the original APK's Java code and assets.
4. Replace the current silent OpenSL ES path before claiming audio support.

No installable APK, on-device launch, rendering, gameplay, or audible output has been verified.


## Stage 28 — latest CI verification and original Android entry-point audit

The documentation commit `988a82e94c6779ad04db23d5537bb4186b18f3a6` was built by workflow run `41507873613`. The run completed successfully. Its `build-arm64` job passed all eight substantive steps: checkout, tool installation, Android NDK/Dynarmic bootstrap, static validators, ARM64 bridge build, ARM32 guest-library staging, and artifact upload. This confirms the latest repository state still builds; it is not an Android runtime test.

The original APK was re-inspected locally (SHA-256 remains `b7aa722b63cef19dd365d57291e0580316253b969768c3f1b1e687d7fd60fab8`). A direct DEX class-data parse reconfirmed:
- `classes.dex`: 120 methods marked `ACC_NATIVE`
- `classes2.dex`: 0 methods marked `ACC_NATIVE`
- Total: 120

The binary Android manifest was decoded. Relevant launch/package facts:
- Package: `com.rovio.angrybirdsgo`
- Version: `2.9.1` (versionCode 29100)
- minSdkVersion 16; targetSdkVersion 26
- Application class: `com.rovio.angrybirdsgo.AngryBirdsGoApplication`
- Launcher Activity: `com.rovio.angrybirdsgo.AngryBirdsGoActivity`
- Native library metadata: `android.app.lib_name=ABK`
- Declares OpenGL ES 2.0 (`glEsVersion=0x20000`); camera and autofocus features are optional.

This changes the packaging plan: the APK already contains the Java Application, launcher Activity, Exient Activity/GL surface classes, manifest, and game assets. A completely new Java Activity wrapper is not the first requirement. Prefer preserving the original Java/asset layer and making the ARM64 package satisfy its existing native-load contract. The manifest's `ABK` library metadata is a strong indication that the app expects the native library under the `libABK.so` name; the bridge/guest-loader packaging must be designed around that without confusing the ARM64 host bridge with the ARM32 guest `libABK.so`.

The main Exient GL view exposes the 17 expected lifecycle/render/input native methods already represented in the generated trampoline table. The exact descriptor-by-descriptor and static/instance comparison across all 120 declarations is still an open validation gate; the matching total alone is not proof of exact parity.

## Updated next gate

1. Finish the exact 120-entry DEX-to-`natives_gen.inc` comparison, including class, name, descriptor, and access flags.
2. Trace the Java library load sequence and confirm how `ABK`, `SkynestSDK`, and `gnustl_shared` are loaded before designing the final ABI-specific filenames and guest asset paths.
3. Verify guest `JNI_OnLoad` and `RegisterNatives` behavior at runtime; 24 declarations still lack a conventional short/long exported symbol match.
4. Build the installable ARM64 APK by retaining the original Java classes/assets and integrating the host bridge plus separately staged ARM32 guest libraries.
5. Replace silent OpenSL ES playback before claiming audio support.

No installable APK, on-device launch, rendering, gameplay, or audible output has been verified.


## Stage 29 — confirmed Java native-library load sequence

A direct instruction-level scan of the original APK's `classes.dex` located the actual `System.loadLibrary(String)` call sites and their argument construction. The key ordering in `AngryBirdsGoActivity.onCreate` is now confirmed:

1. `System.loadLibrary("gnustl_shared")`
2. `System.loadLibrary("SkynestSDK")`
3. Invoke superclass `XGSActivity.onCreate(...)`; the superclass chain reaches `ExientActivity.onCreate(...)`.
4. `ExientActivity.onCreate` reads the activity metadata key `android.app.lib_name` and passes its value to `System.loadLibrary`. The original manifest value is `ABK`, so this loads `libABK.so` after its two explicit dependencies.

This is important: the subclass loads the GNU C++ runtime and Skynest SDK before calling the superclass that loads ABK. A replacement packaging/loader must preserve this ordering or provide equivalent dependency resolution. It must not simply rename the ARM64 host bridge to `libABK.so` while also expecting the ARM32 guest `libABK.so` to be loaded natively by Android.

The DEX also contains AdColony's own `System.loadLibrary` calls for `js` and `adcolony`. These are a separate SDK path and need to be considered when retaining the original Java layer; they are not evidence that the game core itself is loaded through those names.

The DEX parser reconfirmed 120 native declarations in `classes.dex`, zero in `classes2.dex`, with 41 static and 79 instance native declarations. This count is a parser result only; it does not replace the pending exact comparison against the 120 generated bridge rows.

## Updated next gate

1. Compare all 120 DEX declarations against the generated table by class, method, and descriptor; separately validate JNI ABI argument/return types and account for the 41 static declarations.
2. Preserve the Java load order: `gnustl_shared`, `SkynestSDK`, then metadata-selected `ABK`; also account for AdColony's `js` and `adcolony` libraries.
3. Trace runtime SDK `JNI_OnLoad` / `RegisterNatives` calls and resolve the 24 declarations without conventional exported-symbol matches.
4. Implement the Android package so Java's existing load calls reach the ARM64 host bridge while the ARM32 guest libraries remain private inputs to Dynarmic's loader.
5. Replace silent OpenSL ES playback before claiming audio support.

No installable APK, on-device launch, rendering, gameplay, or audible output has been verified.


## Stage 30 — latest bridge build and dynamic library-name path

Workflow run [41509067165](https://github.com/FAntreter/angry-birds-go-port/actions/runs/41509067165) completed successfully on commit `c6b9f03804fa7650cdf003cde1209b59301d1e7a`. All eight substantive steps passed: tool installation, Android NDK/Dynarmic bootstrap, static validators, ARM64 bridge compilation, ARM32 guest-library staging, and artifact upload.

The resulting artifact is:
- Name: `AngryBirdsGoBridge-arm64-v8a`
- Artifact ID: `13172062510`
- Size: 21,855,942 bytes
- SHA-256: `63cccd632e43db98214e237e894e2983e25ac1bddd95478b632ce61edc2fc79e`
- [Download artifact](https://github.com/FAntreter/angry-birds-go-port/actions/runs/41509067165/artifacts/13172062510)

It remains a shared-library bundle, not an installable APK.

A further instruction-level DEX trace resolved the previously noted `Globals.loadLibraries()` path:
- It first attempts `Globals.newInstanceIfAvailable("com.rovio.fusion.Haptics")`.
- It then calls `Globals.getLibraryName()` to construct a diagnostic string and calls `System.loadLibrary(getLibraryName())`.
- `getLibraryName()` returns `Globals.mLibraryName` when non-null; otherwise it returns `Gen.library_name`.

This means there is an additional Java-controlled native library load whose final library name depends on runtime/static field initialization. The bytecode trace establishes the data path, but the final value of `Gen.library_name` / `mLibraryName` has not yet been established. It must be resolved before finalizing the compatibility library-name mapping. The optional Haptics class also needs to be considered when preserving the original Java layer.

A repository-tree audit also found no Android Gradle project, Android manifest, or APK packaging target in the current project tree. The existing CI therefore builds only the ARM64 bridge and stages guest libraries; it does not assemble an application package.

## Updated next gate

1. Resolve the runtime value and initialization source of `Gen.library_name` and `Globals.mLibraryName`, then map every Java `System.loadLibrary` name to the correct ARM64 host/guest-loader behavior.
2. Complete exact descriptor-by-descriptor and static/instance parity validation between the 120 DEX declarations and `natives_gen.inc`; counts alone are insufficient.
3. Capture guest `JNI_OnLoad` / `RegisterNatives` behavior and account for the 24 declarations without conventional short/long exported-symbol matches.
4. Add an Android packaging project that retains the original APK's Java classes, manifest, and assets while keeping ARM32 guest libraries separate from ARM64 host libraries.
5. Replace silent OpenSL ES playback and validate audio, rendering, and lifecycle behavior on an ARM64 device.

No installable APK, on-device launch, rendering, gameplay, or audible output has been verified.


### Stage 30 follow-up — resolved default Fusion library name

The DEX static initializer for `com.rovio.fusionapp.Gen` explicitly assigns `Gen.library_name = "SkynestSDK"`. `Globals.getLibraryName()` therefore falls back to `SkynestSDK` unless `Globals.setLibraryName(String)` has first populated `Globals.mLibraryName`. The setter writes that static override field, and `Globals.loadLibraries()` passes the selected value to `System.loadLibrary`.

So the default Fusion load is `SkynestSDK`, matching the library already loaded explicitly by `AngryBirdsGoActivity`; Android's repeated load of the same library name is expected to reuse the loaded library. A runtime override remains possible, and the call sites that set `mLibraryName` still need to be traced before treating the default as the only path.


## Stage 31 — resolved Fusion library override in the game Activity

The remaining caller trace is now resolved. In `AngryBirdsGoActivity.onCreate(Bundle)`, the bytecode executes these operations in this order:

1. `System.loadLibrary("gnustl_shared")`
2. `System.loadLibrary("SkynestSDK")`
3. `Globals.setLibraryName("ABK")`

The later superclass/Fusion startup path calls `Globals.loadLibraries()`, which uses `Globals.getLibraryName()`; because the Activity has already set `mLibraryName` to `ABK`, this resolves to `System.loadLibrary("ABK")`. The fallback `Gen.library_name = "SkynestSDK"` is therefore not the selected library name for this game Activity path.

This confirms the intended sequence and removes the uncertainty recorded in the Stage 30 follow-up: GNU C++ runtime, Skynest SDK, then metadata/Fusion-selected ABK. It also reinforces that the ARM64 package must intercept the Java load name `ABK` and hand off to the ARM32 guest engine internally; Android must not try to load the guest ARM32 `libABK.so` as a native ARM64 library.


## Stage 32 — ARM64 Java load-name shims and APK assembly path

The original Java code does not load the ARM32 DSOs under their actual ARM32 names through Android's native linker. To preserve its existing `System.loadLibrary` calls on an ARM64-only install, the package now builds ARM64 load stubs for `gnustl_shared`, `SkynestSDK`, `adcolony`, and `js`, while the ARM32 copies retain private `*_arm32.so` filenames beside the bridge. The bridge output is now named `libABK.so`, matching the manifest metadata and Fusion override confirmed in Stage 31.

The stub's JNI_OnLoad returns JNI_VERSION_1_6 and does not replace the ARM32 SDK implementation. The bridge separately loads the real guest dependencies. Direct ELF inspection confirms `libadcolony.so` depends on `libjs.so` and exports its own JNI_OnLoad plus Java native symbols. The bridge now preloads that ARM32 guest pair and invokes AdColony's guest JNI_OnLoad before the final export-binding pass, so its dynamically registered methods can be captured by the existing RegisterNatives interceptor.

A new `scripts/package_original_apk.sh` accepts the original APK, the completed bridge build directory, and an output APK path. It decodes the original APK with apktool, retains its Java DEX/resources/assets/manifest, adds the ARM64 bridge and stubs plus the ARM32 guest payload in `lib/arm64-v8a`, explicitly enables native-library extraction (the bridge reads guest payloads beside `libABK.so`), rebuilds, zipaligns, signs with a local debug key, and verifies the signature.

The build-input validator now checks ABI-specific library naming and the presence of the APK assembly script. This is a packaging implementation path, not yet a generated APK: the original APK must be supplied to the script in the build environment, and apktool/Android build-tools must be installed. Current GitHub CI still validates and builds the native bridge; it does not possess the uploaded original APK and does not yet execute final APK assembly.

## Stage 32 validation boundary

- ARM64 host bridge and load-stub targets must pass the current CI run.
- ARM32 AdColony guest loading and JNI_OnLoad are source changes pending NDK build and runtime validation.
- The package script has not yet been run against the uploaded APK; apktool resource rebuild compatibility and signature/install behavior remain unverified.
- Exact DEX-to-generated-table parity for all 120 methods remains a separate gate.
- No installable APK or device launch/gameplay/audio/rendering test is claimed.
