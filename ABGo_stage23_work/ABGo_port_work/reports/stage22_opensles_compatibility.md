# Stage 22 — OpenSL ES guest-interface compatibility

Added `bridge_src/hle_opensles.cpp` and connected it to HLE initialization and CMake. The compatibility layer registers `slCreateEngine`, defines guest-address-space IID data for the seven OpenSL ES symbols imported by the Go native library, and builds guest-layout vtables for the object, engine, play, Android simple-buffer-queue, and volume interfaces. It supports basic object realization/state, engine output-mix/audio-player creation, play-state calls, queue bookkeeping, and volume state calls.

## Important limitation

This is a **silent compatibility shim**, not real audio playback. It does not forward buffers to the host OpenSL ES engine, does not translate guest callback execution, and does not yet implement all optional interface methods or validate ABI behavior against a running game. It is intended as a first diagnostic step to replace missing-symbol crashes with controlled interface behavior; it may still be insufficient for the game to initialize its audio subsystem.

## Validation

`python3 tools/validate_opensles_hle.py` checks that the imported symbols, guest vtables, interface methods, registration, and CMake source entry remain present. These are source-level checks only. Android NDK compilation and on-device audio testing have not been performed.
