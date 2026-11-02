# Stage 19 — EGL token isolation across displays

## Change

Updated `bridge_src/hle_egl.cpp` so EGL config/context/surface tokens are reused only when both the underlying host handle and the owning EGL display match. Previously, token lookup was keyed only by the host handle, so an identical opaque host handle observed through another display could cause its ownership tag to be overwritten. That could let `eglTerminate` on one display invalidate the guest token mapping associated with another display.

The new `put_owned` helper performs handle lookup and owner assignment under the same mutex. `drop` also removes owner metadata when a config/context/surface token is destroyed. The static lifecycle validator now checks the ownership-aware insertion path.

## Validation

Passed static checks:
- `tools/validate_egl_lifecycle.py`
- `tools/validate_egl_hle.py`
- `tools/validate_build_inputs.py`
- `tools/validate_asset_hle.py`
- `tools/validate_recursive_loader.py`
- `tools/validate_gles_hle.py`
- `tools/validate_dt_needed.py`

## Limitations

This is a source-level fix only. It has not been compiled with the Android NDK or exercised on a device. It does not resolve the remaining OpenSL ES/audio and other runtime ABI gaps, and it does not produce an installable APK. A runtime EGL test with two displays is still needed.
