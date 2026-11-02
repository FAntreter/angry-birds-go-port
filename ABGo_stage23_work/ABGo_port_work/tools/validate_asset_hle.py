#!/usr/bin/env python3
"""Static regression checks for the Android asset-manager HLE additions."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "bridge_src/hle_android.cpp").read_text()
required = {
    "AAssetManager_openDir": "g_AAssetManager_openDir",
    "AAssetDir_getNextFileName": "g_AAssetDir_getNextFileName",
    "AAssetDir_rewind": "g_AAssetDir_rewind",
    "AAssetDir_close": "g_AAssetDir_close",
    "AAsset_openFileDescriptor": "g_AAsset_openFileDescriptor",
}
errors = []
for symbol, function in required.items():
    if f'H("{symbol}", {function})' not in src:
        errors.append(f"missing HLE registration: {symbol}")
    if f"{function}(" not in src:
        errors.append(f"missing wrapper definition: {function}")
for symbol in ("AAsset_close", "AAsset_read", "AAsset_seek", "AAsset_getBuffer"):
    if f'H("{symbol}", g_{symbol})' not in src:
        errors.append(f"regression: existing registration missing: {symbol}")
if "std::lock_guard l(g_mu);" not in src:
    errors.append("expected synchronization guards not found")
if errors:
    print("FAIL")
    print("\n".join(errors))
    raise SystemExit(1)
print(f"PASS: {len(required)} new Android asset imports and 4 existing imports are registered")
print("NOTE: static source check only; not an NDK compile or runtime test")
