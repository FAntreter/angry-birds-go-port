# Stage 23 — OpenSL ES buffer-queue bookkeeping

## Changes

- Buffer enqueue now retains each guest buffer's 32-bit guest address and byte length instead of returning success without recording anything.
- Queue depth is bounded at 64 entries; a full queue returns the OpenSL ES resource-error result instead of accepting unlimited buffers.
- Queue-state queries report the queued-buffer count and play index, and clear empties the queue and resets the index.
- Queue callback registration stores the guest callback address and context for a later emulator callback bridge.
- Queue methods reject non-player objects and invalid/null enqueue arguments.

## Limitations

This is still bookkeeping only. The bridge does not yet consume queued PCM data, resample or convert formats, forward samples to Android audio output, advance the play index, or invoke the stored guest callback. The queue must not be described as working audio playback. Guest callback invocation needs a safe way to enter guest ARM state from host-side asynchronous audio callbacks; doing that without emulator-thread coordination would risk crashes and races.

## Validation

`tools/validate_opensles_queue.py`: all six static checks passed.
`tools/validate_opensles_hle.py`: all ten static checks passed.
Android NDK compilation and on-device testing remain unavailable in this environment.
