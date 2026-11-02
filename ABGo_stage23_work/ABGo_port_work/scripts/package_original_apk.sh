#!/usr/bin/env bash
# Rebuild the user's original APK, preserving its Java/DEX/resources/assets,
# and add the ARM64 bridge + ARM32 guest payload under separate filenames.
set -Eeuo pipefail
if [[ $# -ne 3 ]]; then
  echo "Usage: package_original_apk.sh <original.apk> <bridge-build-dir> <output.apk>" >&2
  exit 2
fi
APK="$(realpath "$1")"
BUILD="$(realpath "$2")"
OUT="$(realpath -m "$3")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for tool in apktool zipalign apksigner; do
  command -v "$tool" >/dev/null || { echo "ERROR: missing required tool: $tool" >&2; exit 3; }
done
[[ -s "$APK" ]] || { echo "ERROR: original APK not found: $APK" >&2; exit 4; }
[[ -s "$BUILD/libABK.so" ]] || { echo "ERROR: ARM64 bridge libABK.so missing in $BUILD" >&2; exit 5; }
for name in libgnustl_shared.so libSkynestSDK.so libadcolony.so libjs.so \
            libABK_arm32.so libgnustl_shared_arm32.so libSkynestSDK_arm32.so \
            libadcolony_arm32.so libjs_arm32.so; do
  [[ -s "$BUILD/$name" ]] || { echo "ERROR: required payload missing: $BUILD/$name" >&2; exit 6; }
done

DECODED="$WORK/decoded"
apktool d -f "$APK" -o "$DECODED"
mkdir -p "$DECODED/lib/arm64-v8a"
for name in libABK.so libgnustl_shared.so libSkynestSDK.so libadcolony.so libjs.so \
            libABK_arm32.so libgnustl_shared_arm32.so libSkynestSDK_arm32.so \
            libadcolony_arm32.so libjs_arm32.so; do
  install -m 0644 "$BUILD/$name" "$DECODED/lib/arm64-v8a/$name"
done

# The app's targetSdkVersion is 26. Explicit extraction is required because
# the bridge reads the private ARM32 payload files beside libABK.so.
python3 - "$DECODED/AndroidManifest.xml" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
app = re.search(r'<application\b', s)
if not app:
    raise SystemExit("ERROR: no <application> element in decoded manifest")
end = s.find('>', app.start())
tag = s[app.start():end]
if 'android:extractNativeLibs=' not in tag:
    s = s[:end] + ' android:extractNativeLibs="true"' + s[end:]
else:
    s = s[:app.start()] + re.sub(r'android:extractNativeLibs="[^"]+"',
                                 'android:extractNativeLibs="true"', tag) + s[end:]
p.write_text(s)
PY

mkdir -p "$(dirname "$OUT")"
UNSIGNED="$WORK/unsigned.apk"
ALIGNED="$WORK/aligned.apk"
apktool b "$DECODED" -o "$UNSIGNED"
zipalign -f -p 4 "$UNSIGNED" "$ALIGNED"

KEYSTORE="${ABGO_DEBUG_KEYSTORE:-$HOME/.android/debug.keystore}"
if [[ ! -s "$KEYSTORE" ]]; then
  mkdir -p "$(dirname "$KEYSTORE")"
  keytool -genkeypair -v -keystore "$KEYSTORE" -storepass android \
    -alias androiddebugkey -keypass android -dname "CN=Android Debug,O=Android,C=US" \
    -keyalg RSA -keysize 2048 -validity 10000
fi
apksigner sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android \
  --out "$OUT" "$ALIGNED"
apksigner verify --verbose "$OUT"
echo "Created signed ARM64 APK: $OUT"
echo "NOTE: package assembly is not proof of successful game startup or gameplay."
