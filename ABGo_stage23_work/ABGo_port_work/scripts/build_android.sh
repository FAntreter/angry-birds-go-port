#!/usr/bin/env bash
# Configure and compile the ARM64 Android bridge. This does not package the game APK.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="${ABGO_DEPS_DIR:-$ROOT/deps}"
NDK="${ANDROID_NDK_HOME:-$DEPS/android-ndk-r30}"
DYN="${DYNARMIC_SRC:-$DEPS/dynarmic}"
BUILD="${ABGO_BUILD_DIR:-$ROOT/build-android-arm64}"
[[ -f "$NDK/build/cmake/android.toolchain.cmake" ]] || { echo "ERROR: NDK missing. Run scripts/bootstrap_android_deps.sh on a network-enabled Linux host." >&2; exit 2; }
[[ -f "$DYN/CMakeLists.txt" && -f "$DYN/src/dynarmic/interface/A32/a32.h" ]] || { echo "ERROR: Dynarmic A32 source missing. Run scripts/bootstrap_android_deps.sh." >&2; exit 2; }
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 \
  -DCMAKE_BUILD_TYPE=Release -DDYNARMIC_SRC="$DYN"
cmake --build "$BUILD" --parallel "${BUILD_JOBS:-2}"
echo "Bridge compilation completed. This is a shared library, not an installable APK."
