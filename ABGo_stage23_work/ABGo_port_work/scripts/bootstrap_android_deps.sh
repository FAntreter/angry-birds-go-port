#!/usr/bin/env bash
# Fetch the official Android NDK and Dynarmic A32 source for the bridge build.
# Requires a network-enabled Linux x86_64 host, curl, unzip, git, sha1sum, CMake, and Ninja.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="${ABGO_DEPS_DIR:-$ROOT/deps}"
NDK_VERSION="r30"
NDK_DIR="$DEPS/android-ndk-$NDK_VERSION"
NDK_ZIP="$DEPS/android-ndk-$NDK_VERSION-linux.zip"
NDK_URL="https://dl.google.com/android/repository/android-ndk-$NDK_VERSION-linux.zip"
NDK_SHA1="5107f898313790e449e87eee2183d9a20602dee9"
DYN_DIR="$DEPS/dynarmic"

for cmd in curl unzip git sha1sum cmake ninja; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "ERROR: required command missing: $cmd" >&2; exit 2; }
done
mkdir -p "$DEPS"
if [[ ! -f "$NDK_DIR/build/cmake/android.toolchain.cmake" ]]; then
  echo "Downloading official Android NDK $NDK_VERSION (~739 MB)..."
  curl --fail --location --retry 3 --continue-at - "$NDK_URL" -o "$NDK_ZIP"
  echo "$NDK_SHA1  $NDK_ZIP" | sha1sum --check --status || { echo "ERROR: NDK SHA-1 verification failed" >&2; exit 3; }
  unzip -q -o "$NDK_ZIP" -d "$DEPS"
fi
[[ -f "$NDK_DIR/build/cmake/android.toolchain.cmake" ]] || { echo "ERROR: NDK toolchain file missing after extraction" >&2; exit 4; }
if [[ ! -f "$DYN_DIR/src/dynarmic/interface/A32/a32.h" ]]; then
  if [[ -e "$DYN_DIR" ]]; then rm -rf "$DYN_DIR"; fi
  echo "Cloning Dynarmic with submodules..."
  git clone --depth 1 --recurse-submodules https://github.com/azahar-emu/dynarmic.git "$DYN_DIR"
fi
[[ -f "$DYN_DIR/CMakeLists.txt" && -f "$DYN_DIR/src/dynarmic/interface/A32/a32.h" ]] || { echo "ERROR: Dynarmic A32 source check failed" >&2; exit 5; }
printf 'ANDROID_NDK_HOME=%s\nDYNARMIC_SRC=%s\n' "$NDK_DIR" "$DYN_DIR"
echo "Dependencies verified. Next run scripts/build_android.sh or use the printed paths in CMake."
