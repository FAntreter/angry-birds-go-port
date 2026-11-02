# Stage 7 — recursive DT_NEEDED loader integration

## Implemented in source

- Replaced the Star Wars-specific root guest filename with `libABK_arm32.so` for the Go port scaffold.
- Added recursive dependency discovery using the bounded `elf::needed()` parser.
- Guest copies of `libgnustl_shared.so`, `libSkynestSDK.so`, `libadcolony.so`, and `libjs.so` are expected under `_arm32.so` filenames to avoid collision with host-ABI libraries.
- Known Android system dependencies are explicitly classified as host/HLE dependencies and are not loaded as ARM32 guest ELFs.
- Guest dependencies load and run constructors before their importing module, so exports are available during ordinary dependency-first relocation.
- Cycles are detected and fail explicitly instead of recursing forever.

## Packaging contract

The host bridge directory must contain `libABK_arm32.so` and any non-system ARM32 dependencies under their mapped `_arm32.so` filenames. Missing guest files are fatal with a diagnostic naming the expected filename.

## Limitations / unresolved correctness work

This is a first-pass dependency loader, not a complete Android dynamic linker. It intentionally rejects cyclic dependency graphs because the current ELF implementation maps, exports, and relocates one module in a single operation. It does not yet implement symbol versioning, GNU hash-only symbol counts, TLS, RELA/RELR, full Android linker namespaces, or robust `DT_RPATH`/`DT_RUNPATH` search. A library classified as host/HLE may still have imports that require explicit HLE functions or guest data symbols; the classification only controls whether to load a guest ELF. The embedded runtime is initialized before the Go dependency tree.

## Validation

`tools/validate_recursive_loader.py` is a source-level regression check only. No Android NDK compile, APK packaging, emulator boot, or physical-device run has been performed. In particular, the host-library list and filename mapping must be checked against the exact final APK package before runtime testing.
