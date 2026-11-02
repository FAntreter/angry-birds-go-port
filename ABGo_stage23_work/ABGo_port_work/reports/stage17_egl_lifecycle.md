# Stage 17 — EGL token lifecycle hardening

## Change
`eglTerminate` now rejects an invalid display token and, when the host EGL call succeeds, removes the display token and clears cached config/context/surface token maps. The guest sees opaque 32-bit IDs rather than host pointers; retaining these maps after termination could leave stale host handles reachable.

## Validation
- `tools/validate_egl_lifecycle.py` passes.
- `tools/validate_egl_hle.py` passes.
- `tools/validate_build_inputs.py` passes.
- `tools/validate_asset_hle.py` passes.
- `tools/validate_recursive_loader.py` passes.
- `tools/validate_gles_hle.py` passes.
- ZIP integrity check passes.

## Limitations
This is source-level lifecycle hardening, not evidence of working rendering. It currently clears all EGL config/context/surface token maps because the bridge does not associate those tokens with a specific display. This assumes a single guest EGL display; multi-display correctness requires per-display ownership tracking. No Android NDK compile or device test was possible in this environment. OpenSL ES remains unresolved and there is no playable APK.
