# Stage 6 — Android asset-manager HLE coverage

## Implemented in source

`bridge_src/hle_android.cpp` now registers wrappers for these additional `libandroid.so` imports:

- `AAssetManager_openDir`
- `AAssetDir_getNextFileName`
- `AAssetDir_rewind`
- `AAssetDir_close`
- `AAsset_openFileDescriptor`

Directory handles are represented by guest tokens mapped to host `AAssetDir*` values. Returned filenames are copied into guest memory and the previous guest filename is released on the next filename request, rewind, or close. The file-descriptor wrapper calls the host NDK API and copies the start/length offsets to guest memory. Existing asset lookup/read/seek/length/buffer operations were adjusted to keep map lookups and host operations under the same mutex, avoiding a race where an asset could be closed while a wrapper was using its map entry.

## Validation

Run:

```sh
python3 tools/validate_asset_hle.py
```

The static regression check passes for all five new registrations and four existing registrations.

## Limitations / follow-up

- This is a source-level implementation only. It has **not** been compiled with the Android NDK or exercised on a device in this environment.
- `AAsset_openFileDescriptor` guest offset width must be confirmed against the target ARM32 NDK headers/build flags. The wrapper currently uses 64-bit offsets, matching modern Android NDK `off_t` configurations; if the original game was built with a 32-bit `off_t` ABI, this wrapper must be adjusted to that ABI.
- The wrapper assumes the game uses the returned file descriptor as a host descriptor, which is appropriate for an in-process bridge but still needs runtime confirmation.
- EGL, `ANativeWindow`, OpenSL ES, `libjnigraphics`, recursive guest dependency loading, and cross-module relocation remain unresolved. This stage does not create a playable APK.
