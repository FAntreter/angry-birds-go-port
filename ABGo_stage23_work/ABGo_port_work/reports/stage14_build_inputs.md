# Stage 14 — restore required generated sources and prepare an Android build target

## Concrete blockers fixed

The previous source-only package could not compile as checked out because three generated include files referenced directly by the C++ sources were absent: `guest_rt_blob.inc`, `kuser_blob.inc`, and `natives_gen.inc`. Stage 14 restores the two runtime blobs from the supplied reference source and regenerates the native trampoline table from the Angry Birds Go! DEX native-method inventory (120 unique declarations), rather than reusing the Star Wars game's native-method table.

The missing `gheap.c` implementation was also restored because `arena.cpp` calls the `gheap_*` API. A root `CMakeLists.txt` now describes an Android `arm64-v8a` shared-library target, all bridge source files, system libraries, and the Dynarmic A32 frontend dependency. `tools/generate_go_natives.py` documents/regenerates the Go-specific trampoline table from `analysis/go_native_methods.json`; `tools/validate_build_inputs.py` checks these build inputs.

## Validation

- `tools/validate_build_inputs.py`: passed.
- Existing static dependency, asset-HLE, GLES-HLE, recursive-loader, relocation, and packaging audits: run separately; these remain static checks only.
- Android NDK compilation: **not run**. This environment does not have `ndk-build`, Android NDK toolchain files, `android.jar`, or a Dynarmic source checkout. The CMake target therefore cannot yet be built here.

## Important limitations

This stage fixes missing source/build inputs; it does not implement EGL/native-window forwarding or OpenSL ES compatibility. The JNI trampoline list is based on DEX declarations, but runtime registration/export matching still needs validation on device. No APK has been built, installed, or tested. A reproducible Android build still requires the NDK and a compatible Dynarmic checkout.
