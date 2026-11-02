# Stage 12 — guest payload filename staging

## Change

Added `tools/stage_guest_libs.py`, which validates the ELF class, byte order, and machine ID of bundled guest libraries, then copies them to the exact ARM32-suffixed filenames expected by `bridge.cpp`'s `guest_filename()` mapping.

Required files staged by default:
- `libABK.so` → `libABK_arm32.so`
- `libgnustl_shared.so` → `libgnustl_shared_arm32.so`
- `libSkynestSDK.so` → `libSkynestSDK_arm32.so`

Optional ads/JS libraries can be included with `--include-optional`.

## Validation

The script successfully staged and validated the three required guest ELF files from `target_libs/`. It checks ELFCLASS32, little-endian encoding, and `EM_ARM` before copying. A separate verification confirmed the staged outputs retain the expected ELF identity.

## Important limitations

This closes a concrete packaging mismatch between the recursive loader's expected filenames and the raw source filenames. It does **not** create an APK or compile the host bridge. EGL/native-window and OpenSL ES support remain unresolved; the package has not been built with the Android NDK or tested on a device.

## Usage

```sh
python3 tools/stage_guest_libs.py --out /path/to/jniLibs/arm64-v8a/guest
```

Add `--include-optional` to stage `libadcolony_arm32.so` and `libjs_arm32.so` too. Confirm the runtime path in the eventual Android packaging code matches this output directory.
