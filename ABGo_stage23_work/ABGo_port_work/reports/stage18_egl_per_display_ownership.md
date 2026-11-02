# Stage 18 — per-display EGL resource ownership

## Change

The EGL bridge previously cleared all config, context, and surface token tables after any successful `eglTerminate`. This could invalidate resources associated with another EGL display. Stage 18 adds owner maps for configs, contexts, and surfaces, tags resources returned or created through a display, and removes only tokens tagged to the display being terminated. Current-context/current-surface queries also associate the returned tokens with `eglGetCurrentDisplay()` when available.

## Validation

- `tools/validate_egl_lifecycle.py` passes and rejects global resource-table clearing.
- Existing static checks are run against this package; results are recorded in `stage18_validation.txt`.
- ZIP integrity is checked after packaging.

## Limitations

This remains source-level work. Owner tracking uses token-level ownership and assumes each EGL config/context/surface token has a single owning display. No Android NDK compile or device test was performed. EGL thread-local semantics and broader ABI correctness still need runtime validation. OpenSL ES compatibility remains unfinished, and there is no playable APK.
