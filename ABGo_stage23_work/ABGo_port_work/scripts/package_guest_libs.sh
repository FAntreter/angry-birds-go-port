#!/usr/bin/env bash
# Stage the ARM32 guest DSOs beside the ARM64 bridge.
# The bridge opens these files by path; they are data for its ELF loader,
# not libraries for Android's native linker to load.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${1:-$ROOT/target_libs}"
DEST="${2:?Usage: package_guest_libs.sh [target_libs_dir] <bridge_native_lib_dir>}"
command -v readelf >/dev/null || { echo "ERROR: readelf is required" >&2; exit 2; }
mkdir -p "$DEST"

declare -A names=(
  [libABK.so]=libABK_arm32.so
  [libgnustl_shared.so]=libgnustl_shared_arm32.so
  [libSkynestSDK.so]=libSkynestSDK_arm32.so
  [libadcolony.so]=libadcolony_arm32.so
  [libjs.so]=libjs_arm32.so
)
for source_name in "${!names[@]}"; do
  source="$SRC/$source_name"
  [[ -s "$source" ]] || { echo "ERROR: missing guest library: $source" >&2; exit 3; }
  header="$(readelf -h "$source")"
  grep -q 'Class:.*ELF32' <<<"$header" || { echo "ERROR: $source_name is not ELF32" >&2; exit 4; }
  grep -q 'Machine:.*ARM' <<<"$header" || { echo "ERROR: $source_name is not ARM" >&2; exit 5; }
  install -m 0644 "$source" "$DEST/${names[$source_name]}"
  echo "Staged ${names[$source_name]}"
done
echo "Guest ARM32 libraries staged beside the bridge in: $DEST"
