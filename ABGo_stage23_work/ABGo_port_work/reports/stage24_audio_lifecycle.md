# Stage 24 — OpenSL ES lifecycle and queue hardening

## Changes

- Added per-object state locking so queue operations and play-state reads/writes do not race on shared mutable state.
- Queue enqueue now checks that the requested guest buffer range lies inside the bridge's guest address space before retaining its address and length.
- Destroy marks the object invalid, resets playback state, clears queued buffers, and drops stored callback/context addresses. Static interface cells remain allocated so stale guest pointers do not become host use-after-free pointers; subsequent guarded operations reject destroyed objects.
- Added `tools/validate_opensles_lifecycle.py` to check these invariants.

## Validation

`validate_opensles_queue.py`, `validate_opensles_hle.py`, and `validate_opensles_lifecycle.py` pass. `preflight_android_build.py` reports that an Android NDK and a Dynarmic A32 source checkout are missing, so C++ compilation was not attempted.

## Limitations

This is lifecycle/concurrency hardening only. It does not produce audio, invoke guest queue callbacks, or establish ABI correctness by runtime testing. The bridge remains uncompiled and there is no verified APK.
