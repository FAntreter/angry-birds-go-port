# Stage 25 — Android NDK and Dynarmic dependency bootstrap

## What changed

- Added `scripts/bootstrap_android_deps.sh` to fetch Android NDK r30 from Google's official repository, verify the published SHA-1 before extraction, and clone Dynarmic with its submodules.
- Added `scripts/build_android.sh` to configure the ARM64 Android target with the NDK toolchain and compile the bridge with CMake/Ninja.
- Both scripts stop with explicit errors when required dependencies are absent; they do not claim a build succeeded merely because configuration inputs exist.

## Why the dependencies are not installed in this workspace

This execution environment has no Android NDK and no Dynarmic checkout. A direct `git ls-remote` request to GitHub failed because DNS/network access is unavailable. The official Linux NDK package is about 739 MB, so it cannot be responsibly replaced by a guessed or partial download. The bootstrap script is ready for a network-enabled Linux x86_64 build host, but it has **not** been run to completion here.

## Verified facts and limits

- `cmake` and `ninja` are present locally.
- `ANDROID_NDK_HOME` and `DYNARMIC_SRC` are unset.
- No `android.toolchain.cmake`, Dynarmic `a32.h`, or Dynarmic shared library was found under `/mnt/data`.
- The build script has not been executed successfully; no `.so` or APK is claimed.
- NDK r30 Linux SHA-1 pinned from the official Android NDK downloads page: `5107f898313790e449e87eee2183d9a20602dee9`.

## References

- Android NDK downloads: https://developer.android.com/ndk/downloads
- Dynarmic A32 frontend: https://github.com/azahar-emu/dynarmic
