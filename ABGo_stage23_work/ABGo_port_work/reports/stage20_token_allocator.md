# Stage 20 — safer guest handle tokens and display ownership checks

## Changes

- Centralized guest-token allocation in `allocate_token_locked()`. EGL displays, configs, contexts, surfaces, and `ANativeWindow*` values share one guest-visible token namespace. The allocator checks every live handle table before returning a token, including after counter wrap, and reports exhaustion as token `0` instead of overwriting a live mapping.
- Balanced the `ANativeWindow_fromSurface` reference if token allocation fails.
- Added display-ownership checks before EGL config/context/surface operations. Cross-display or stale nonzero tokens are rejected instead of being passed to host EGL as if they were valid handles. The lookup helper checks the handle and owner under the same mutex.

## Validation

- `tools/validate_egl_token_allocator.py`: shared token namespace, wraparound scan, safe exhaustion, and native-window reference balancing.
- `tools/validate_egl_token_ownership.py`: checks the ownership guard is present on config queries, context/surface create/destroy, make-current, swap, and surface queries/attributes.
- Existing EGL lifecycle, EGL registration, build-input, asset, recursive-loader, GLES, and DT_NEEDED static checks were rerun.

These are static source checks only. The changes have not been NDK-compiled or device-tested; concurrency and real EGL behavior still require runtime testing.
