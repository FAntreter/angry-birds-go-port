#!/usr/bin/env python3
"""Static regression checks for the additional GLES2 HLE wrappers."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
src = (root / "bridge_src/hle_gles.cpp").read_text()
required = {
    "glBindAttribLocation": "g_BindAttribLocation",
    "glBlendEquationSeparate": "g_BlendEquationSeparate",
    "glBlendFuncSeparate": "g_BlendFuncSeparate",
    "glBufferSubData": "g_BufferSubData",
    "glClearDepthf": "g_ClearDepthf",
    "glClearStencil": "g_ClearStencil",
    "glColorMask": "g_ColorMask",
    "glCompressedTexSubImage2D": "g_CompressedTexSubImage2D",
    "glGenerateMipmap": "g_GenerateMipmap",
    "glGetFloatv": "g_GetFloatv",
    "glStencilFunc": "g_StencilFunc",
    "glStencilMask": "g_StencilMask",
    "glStencilOp": "g_StencilOp",
    "glTexParameterf": "g_TexParameterf",
    "glUniform1fv": "g_Uniform1fv",
    "glUniform2fv": "g_Uniform2fv",
    "glUniform3fv": "g_Uniform3fv",
    "glUniform4fv": "g_Uniform4fv",
}
errors = []
for symbol, fn in required.items():
    if f'H("{symbol}", {fn})' not in src:
        errors.append(f"missing registration: {symbol}")
    if f"{fn}(" not in src:
        errors.append(f"missing wrapper: {fn}")
if errors:
    print("FAIL\n" + "\n".join(errors))
    raise SystemExit(1)
print(f"PASS: {len(required)} additional GLES2 imports have wrappers and registrations")
print("NOTE: static source check only; Android NDK compile/runtime test not performed")
