# Stage 3 — bounded DT_NEEDED parser

## Source change

Added `elf::needed(const uint8_t*, size_t)` to `bridge_src/elf32.h/.cpp`. It reads `PT_DYNAMIC`, collects `DT_NEEDED`, locates the file-backed `DT_STRTAB` via a `PT_LOAD` segment, checks string bounds, and returns dependency names before guest mapping. It rejects non-ARM32 ET_DYN inputs and malformed tables.

## Validation performed

- Host C++ syntax check of `elf32.cpp` passed using a tiny stub for Android logging headers: `g++ -std=c++20 -fsyntax-only`.
- Independent Python ELF32 parser matched `readelf -d` exactly for all five bundled ARM32 libraries:
  - `libABK.so`: 12 dependencies
  - `libSkynestSDK.so`: 7
  - `libadcolony.so`: 7
  - `libgnustl_shared.so`: 3
  - `libjs.so`: 5
- The validator is `tools/validate_dt_needed.py`.

## Limitations

This validates dependency *discovery*, not a full Android NDK build. `elf::needed` is not yet wired into recursive loading. The loader still lacks dependency graph orchestration and complete cross-module relocation/initialization semantics. No APK was built or device-tested.
