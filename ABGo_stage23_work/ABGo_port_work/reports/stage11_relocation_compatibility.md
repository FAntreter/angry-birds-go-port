# Stage 11 — ARM32 relocation compatibility audit

This report compares relocation names found by `readelf -rW` with the relocation cases currently implemented in `bridge_src/elf32.cpp`. It is static analysis, not a build or runtime test.

## Per-library relocation inventory

### `libABK.so`
- `R_ARM_ABS32`: 2817 — supported by current loader
- `R_ARM_GLOB_DAT`: 35 — supported by current loader
- `R_ARM_JUMP_SLOT`: 502 — supported by current loader
- `R_ARM_RELATIVE`: 34872 — supported by current loader

### `libSkynestSDK.so`
- `R_ARM_ABS32`: 1289 — supported by current loader
- `R_ARM_GLOB_DAT`: 52 — supported by current loader
- `R_ARM_JUMP_SLOT`: 308 — supported by current loader
- `R_ARM_RELATIVE`: 8715 — supported by current loader

### `libadcolony.so`
- `R_ARM_GLOB_DAT`: 7 — supported by current loader
- `R_ARM_JUMP_SLOT`: 96 — supported by current loader
- `R_ARM_RELATIVE`: 34 — supported by current loader

### `libgnustl_shared.so`
- `R_ARM_GLOB_DAT`: 5 — supported by current loader
- `R_ARM_JUMP_SLOT`: 115 — supported by current loader
- `R_ARM_RELATIVE`: 2599 — supported by current loader

### `libjs.so`
- `R_ARM_ABS32`: 290 — supported by current loader
- `R_ARM_GLOB_DAT`: 25 — supported by current loader
- `R_ARM_JUMP_SLOT`: 221 — supported by current loader
- `R_ARM_RELATIVE`: 6163 — supported by current loader

## Summary

- Relocation types observed: **4**.
- Relocation entries whose type is not recognized by the current loader: **0**.
- The parser may not see every relocation format if a library uses RELA, RELR, Android packed relocations, or a toolchain-specific format.

## Current supported set
`R_ARM_NONE`, `R_ARM_ABS32`, `R_ARM_REL32`, `R_ARM_GLOB_DAT`, `R_ARM_JUMP_SLOT`, `R_ARM_RELATIVE`

## Important limitations
- A supported relocation type does not prove symbol lookup order, addend handling, weak-symbol semantics, or relocation target bounds are correct.
- Unsupported relocation types are fatal in the current loader; do not treat an import audit alone as evidence that loading will succeed.
- Unresolved import messages now identify the importing guest library and explicitly state when a diagnostic thunk is installed. Such thunks are not real implementations.

## Re-run
`python3 tools/audit_elf_relocations.py`
