# Stage 10 — additional GLES2 HLE wrappers

Added wrappers and HLE registrations for 18 GLES2 imports identified by the Go native-library audit:

- `glBindAttribLocation`
- `glBlendEquationSeparate`, `glBlendFuncSeparate`
- `glBufferSubData`
- `glClearDepthf`, `glClearStencil`, `glColorMask`
- `glCompressedTexSubImage2D`, `glGenerateMipmap`, `glGetFloatv`
- `glStencilFunc`, `glStencilMask`, `glStencilOp`
- `glTexParameterf`
- `glUniform1fv`, `glUniform2fv`, `glUniform3fv`, `glUniform4fv`

Pointer arguments are translated through the guest arena. The `glBufferSubData` wrapper deliberately receives a 32-bit offset because `GLintptr` is 32-bit in the ARM32 guest ABI, even though it is 64-bit in the host ABI.

## Validation

`python3 tools/validate_gles_hle.py` passes; existing asset HLE and recursive loader source checks pass. This is static validation only. No Android NDK compile or on-device graphics test was available in this stage.

## Still blocking real graphics

These wrappers do not implement EGL context/surface creation or `ANativeWindow_setBuffersGeometry`. The game cannot be considered playable until the host EGL context lifecycle is connected to the game's Java `Surface`/native-window lifecycle and tested on-device. Symbol presence alone is not proof of ABI or runtime correctness.
