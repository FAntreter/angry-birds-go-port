// GLES 2.0 imports. Pointer arguments address guest memory, which the host GL reads in place.
#include <GLES2/gl2.h>

#include <mutex>
#include <unordered_map>
#include <vector>

#include "hle.h"

namespace {

using arena::hp;

// Client-side arrays vs. buffer offsets depend on the current bindings (GL thread only).
thread_local GLuint t_array_buffer = 0;
thread_local GLuint t_element_buffer = 0;

const void* attrib_ptr(uint32_t p, GLuint bound) {
    return bound ? reinterpret_cast<const void*>(uintptr_t{p}) : (p ? arena::h(p) : nullptr);
}

void g_ActiveTexture(uint32_t t) { glActiveTexture(t); }
// Additional GLES2 entry points used by Angry Birds Go. Guest pointers are translated
// through the 32-bit arena before the host GLES driver sees them.
void g_BindAttribLocation(uint32_t p, uint32_t i, GStr name) { glBindAttribLocation(p, i, name.h()); }
void g_BlendEquationSeparate(uint32_t rgb, uint32_t alpha) { glBlendEquationSeparate(rgb, alpha); }
void g_BlendFuncSeparate(uint32_t sr, uint32_t dr, uint32_t sa, uint32_t da) { glBlendFuncSeparate(sr, dr, sa, da); }
void g_BufferSubData(uint32_t target, int32_t offset, int32_t size, GPtr<const void> data) {
    // GLintptr is 32-bit in the ARM32 guest ABI even though it is 64-bit in the host ABI.
    glBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data.h());
}
void g_ClearDepthf(float depth) { glClearDepthf(depth); }
void g_ClearStencil(int32_t s) { glClearStencil(s); }
void g_ColorMask(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    glColorMask(static_cast<GLboolean>(r), static_cast<GLboolean>(g), static_cast<GLboolean>(b), static_cast<GLboolean>(a));
}
void g_CompressedTexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t w, int32_t h,
                               uint32_t format, int32_t imageSize, GPtr<const void> data) {
    glCompressedTexSubImage2D(target, level, x, y, w, h, format, imageSize, data.h());
}
void g_GenerateMipmap(uint32_t target) { glGenerateMipmap(target); }
void g_GetFloatv(uint32_t pname, GPtr<float> out) { glGetFloatv(pname, out.h()); }
void g_StencilFunc(uint32_t func, int32_t ref, uint32_t mask) { glStencilFunc(func, ref, mask); }
void g_StencilMask(uint32_t mask) { glStencilMask(mask); }
void g_StencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) { glStencilOp(fail, zfail, zpass); }
void g_TexParameterf(uint32_t target, uint32_t pname, float param) { glTexParameterf(target, pname, param); }
void g_Uniform1fv(int32_t loc, int32_t count, GPtr<const float> value) { glUniform1fv(loc, count, value.h()); }
void g_Uniform2fv(int32_t loc, int32_t count, GPtr<const float> value) { glUniform2fv(loc, count, value.h()); }
void g_Uniform3fv(int32_t loc, int32_t count, GPtr<const float> value) { glUniform3fv(loc, count, value.h()); }
void g_Uniform4fv(int32_t loc, int32_t count, GPtr<const float> value) { glUniform4fv(loc, count, value.h()); }
void g_AttachShader(uint32_t p, uint32_t s) { glAttachShader(p, s); }
void g_BindBuffer(uint32_t target, uint32_t b) {
    if (target == GL_ARRAY_BUFFER) t_array_buffer = b;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) t_element_buffer = b;
    glBindBuffer(target, b);
}
void g_BindFramebuffer(uint32_t t, uint32_t f) { glBindFramebuffer(t, f); }
void g_BindRenderbuffer(uint32_t t, uint32_t r) { glBindRenderbuffer(t, r); }
void g_BindTexture(uint32_t t, uint32_t tex) { glBindTexture(t, tex); }
void g_BlendEquation(uint32_t m) { glBlendEquation(m); }
void g_BlendFunc(uint32_t s, uint32_t d) { glBlendFunc(s, d); }
void g_BufferData(uint32_t t, int32_t size, GPtr<const void> data, uint32_t usage) { glBufferData(t, size, data.h(), usage); }
void g_Clear(uint32_t m) { glClear(m); }
void g_ClearColor(float r, float g, float b, float a) { glClearColor(r, g, b, a); }
void g_CompileShader(uint32_t s) { glCompileShader(s); }
void g_CompressedTexImage2D(uint32_t t, int32_t l, uint32_t f, int32_t w, int32_t h, int32_t b, int32_t size,
                            GPtr<const void> data) {
    glCompressedTexImage2D(t, l, f, w, h, b, size, data.h());
}
void g_CopyTexImage2D(uint32_t t, int32_t l, uint32_t f, int32_t x, int32_t y, int32_t w, int32_t h, int32_t b) {
    glCopyTexImage2D(t, l, f, x, y, w, h, b);
}
uint32_t g_CreateProgram() { return glCreateProgram(); }
uint32_t g_CreateShader(uint32_t t) { return glCreateShader(t); }
void g_CullFace(uint32_t m) { glCullFace(m); }
void g_DeleteBuffers(int32_t n, GPtr<const uint32_t> ids) {
    for (int32_t i = 0; i < n; ++i) {
        if (ids.h()[i] == t_array_buffer) t_array_buffer = 0;
        if (ids.h()[i] == t_element_buffer) t_element_buffer = 0;
    }
    glDeleteBuffers(n, ids.h());
}
void g_DeleteFramebuffers(int32_t n, GPtr<const uint32_t> ids) { glDeleteFramebuffers(n, ids.h()); }
void g_DeleteProgram(uint32_t p) { glDeleteProgram(p); }
void g_DeleteRenderbuffers(int32_t n, GPtr<const uint32_t> ids) { glDeleteRenderbuffers(n, ids.h()); }
void g_DeleteShader(uint32_t s) { glDeleteShader(s); }
void g_DeleteTextures(int32_t n, GPtr<const uint32_t> ids) { glDeleteTextures(n, ids.h()); }
void g_DepthFunc(uint32_t f) { glDepthFunc(f); }
void g_DepthMask(uint32_t f) { glDepthMask(static_cast<GLboolean>(f)); }
void g_DetachShader(uint32_t p, uint32_t s) { glDetachShader(p, s); }
void g_Disable(uint32_t c) { glDisable(c); }
void g_DisableVertexAttribArray(uint32_t i) { glDisableVertexAttribArray(i); }
void g_DrawArrays(uint32_t m, int32_t f, int32_t c) { glDrawArrays(m, f, c); }
void g_DrawElements(uint32_t m, int32_t c, uint32_t t, uint32_t indices) {
    glDrawElements(m, c, t, attrib_ptr(indices, t_element_buffer));
}
void g_Enable(uint32_t c) { glEnable(c); }
void g_EnableVertexAttribArray(uint32_t i) { glEnableVertexAttribArray(i); }
void g_Finish() { glFinish(); }
void g_Flush() { glFlush(); }
void g_FramebufferRenderbuffer(uint32_t t, uint32_t a, uint32_t rt, uint32_t r) { glFramebufferRenderbuffer(t, a, rt, r); }
void g_FramebufferTexture2D(uint32_t t, uint32_t a, uint32_t tt, uint32_t tex, int32_t l) {
    glFramebufferTexture2D(t, a, tt, tex, l);
}
void g_FrontFace(uint32_t m) { glFrontFace(m); }
void g_GenBuffers(int32_t n, GPtr<uint32_t> ids) { glGenBuffers(n, ids.h()); }
void g_GenFramebuffers(int32_t n, GPtr<uint32_t> ids) { glGenFramebuffers(n, ids.h()); }
void g_GenRenderbuffers(int32_t n, GPtr<uint32_t> ids) { glGenRenderbuffers(n, ids.h()); }
void g_GenTextures(int32_t n, GPtr<uint32_t> ids) { glGenTextures(n, ids.h()); }
void g_GetActiveUniform(uint32_t p, uint32_t i, int32_t bufsize, GPtr<int32_t> len, GPtr<int32_t> size,
                        GPtr<uint32_t> type, GPtr<char> name) {
    glGetActiveUniform(p, i, bufsize, len.h(), size.h(), type.h(), name.h());
}
int32_t g_GetAttribLocation(uint32_t p, GStr n) { return glGetAttribLocation(p, n.h()); }
void g_GetIntegerv(uint32_t pname, GPtr<int32_t> out) { glGetIntegerv(pname, out.h()); }
void g_GetProgramInfoLog(uint32_t p, int32_t n, GPtr<int32_t> len, GPtr<char> log) { glGetProgramInfoLog(p, n, len.h(), log.h()); }
void g_GetProgramiv(uint32_t p, uint32_t pname, GPtr<int32_t> out) { glGetProgramiv(p, pname, out.h()); }
void g_GetShaderInfoLog(uint32_t s, int32_t n, GPtr<int32_t> len, GPtr<char> log) { glGetShaderInfoLog(s, n, len.h(), log.h()); }
void g_GetShaderiv(uint32_t s, uint32_t pname, GPtr<int32_t> out) { glGetShaderiv(s, pname, out.h()); }

std::mutex g_str_mu;
std::unordered_map<uint32_t, uint32_t> g_strings;
uint32_t g_GetString(uint32_t name) {
    const auto* s = reinterpret_cast<const char*>(glGetString(name));
    if (!s) return 0;
    std::lock_guard l(g_str_mu);
    auto& slot = g_strings[name];
    if (!slot || strcmp(hp<const char>(slot), s) != 0) slot = arena::static_strdup(s);
    return slot;
}
int32_t g_GetUniformLocation(uint32_t p, GStr n) { return glGetUniformLocation(p, n.h()); }
void g_GetUniformfv(uint32_t p, int32_t loc, GPtr<float> out) { glGetUniformfv(p, loc, out.h()); }
void g_LinkProgram(uint32_t p) { glLinkProgram(p); }
void g_PixelStorei(uint32_t pname, int32_t v) { glPixelStorei(pname, v); }
void g_ReadPixels(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t f, uint32_t t, GPtr<void> data) {
    glReadPixels(x, y, w, h, f, t, data.h());
}
void g_RenderbufferStorage(uint32_t t, uint32_t f, int32_t w, int32_t h) { glRenderbufferStorage(t, f, w, h); }
void g_Scissor(int32_t x, int32_t y, int32_t w, int32_t h) { glScissor(x, y, w, h); }
void g_ShaderSource(uint32_t s, int32_t count, GPtr<const uint32_t> strings, GPtr<const int32_t> lengths) {
    std::vector<const char*> ptrs(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) ptrs[static_cast<size_t>(i)] = hp<const char>(strings.h()[i]);
    glShaderSource(s, count, ptrs.data(), lengths.h());
}
void g_TexImage2D(uint32_t t, int32_t l, int32_t ifmt, int32_t w, int32_t h, int32_t b, uint32_t f, uint32_t ty,
                  GPtr<const void> data) {
    glTexImage2D(t, l, ifmt, w, h, b, f, ty, data.h());
}
void g_TexParameteri(uint32_t t, uint32_t p, int32_t v) { glTexParameteri(t, p, v); }
void g_TexSubImage2D(uint32_t t, int32_t l, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t f, uint32_t ty,
                     GPtr<const void> data) {
    glTexSubImage2D(t, l, x, y, w, h, f, ty, data.h());
}
void g_Uniform1f(int32_t loc, float v) { glUniform1f(loc, v); }
void g_Uniform1i(int32_t loc, int32_t v) { glUniform1i(loc, v); }
void g_Uniform4f(int32_t loc, float a, float b, float c, float d) { glUniform4f(loc, a, b, c, d); }
void g_UniformMatrix4fv(int32_t loc, int32_t n, uint32_t tr, GPtr<const float> v) {
    glUniformMatrix4fv(loc, n, static_cast<GLboolean>(tr), v.h());
}
void g_UseProgram(uint32_t p) { glUseProgram(p); }
void g_ValidateProgram(uint32_t p) { glValidateProgram(p); }
void g_VertexAttribPointer(uint32_t i, int32_t size, uint32_t type, uint32_t norm, int32_t stride, uint32_t ptr) {
    glVertexAttribPointer(i, size, type, static_cast<GLboolean>(norm), stride, attrib_ptr(ptr, t_array_buffer));
}
void g_Viewport(int32_t x, int32_t y, int32_t w, int32_t h) { glViewport(x, y, w, h); }

const hle::Def kDefs[] = {
    H("glBindAttribLocation", g_BindAttribLocation), H("glBlendEquationSeparate", g_BlendEquationSeparate),
    H("glBlendFuncSeparate", g_BlendFuncSeparate), H("glBufferSubData", g_BufferSubData),
    H("glClearDepthf", g_ClearDepthf), H("glClearStencil", g_ClearStencil), H("glColorMask", g_ColorMask),
    H("glCompressedTexSubImage2D", g_CompressedTexSubImage2D), H("glGenerateMipmap", g_GenerateMipmap),
    H("glGetFloatv", g_GetFloatv), H("glStencilFunc", g_StencilFunc), H("glStencilMask", g_StencilMask),
    H("glStencilOp", g_StencilOp), H("glTexParameterf", g_TexParameterf), H("glUniform1fv", g_Uniform1fv),
    H("glUniform2fv", g_Uniform2fv), H("glUniform3fv", g_Uniform3fv), H("glUniform4fv", g_Uniform4fv),
    H("glActiveTexture", g_ActiveTexture), H("glAttachShader", g_AttachShader), H("glBindBuffer", g_BindBuffer),
    H("glBindFramebuffer", g_BindFramebuffer), H("glBindRenderbuffer", g_BindRenderbuffer),
    H("glBindTexture", g_BindTexture), H("glBlendEquation", g_BlendEquation), H("glBlendFunc", g_BlendFunc),
    H("glBufferData", g_BufferData), H("glClear", g_Clear), H("glClearColor", g_ClearColor),
    H("glCompileShader", g_CompileShader), H("glCompressedTexImage2D", g_CompressedTexImage2D),
    H("glCopyTexImage2D", g_CopyTexImage2D), H("glCreateProgram", g_CreateProgram),
    H("glCreateShader", g_CreateShader), H("glCullFace", g_CullFace), H("glDeleteBuffers", g_DeleteBuffers),
    H("glDeleteFramebuffers", g_DeleteFramebuffers), H("glDeleteProgram", g_DeleteProgram),
    H("glDeleteRenderbuffers", g_DeleteRenderbuffers), H("glDeleteShader", g_DeleteShader),
    H("glDeleteTextures", g_DeleteTextures), H("glDepthFunc", g_DepthFunc), H("glDepthMask", g_DepthMask),
    H("glDetachShader", g_DetachShader), H("glDisable", g_Disable),
    H("glDisableVertexAttribArray", g_DisableVertexAttribArray), H("glDrawArrays", g_DrawArrays),
    H("glDrawElements", g_DrawElements), H("glEnable", g_Enable),
    H("glEnableVertexAttribArray", g_EnableVertexAttribArray), H("glFinish", g_Finish), H("glFlush", g_Flush),
    H("glFramebufferRenderbuffer", g_FramebufferRenderbuffer), H("glFramebufferTexture2D", g_FramebufferTexture2D),
    H("glFrontFace", g_FrontFace), H("glGenBuffers", g_GenBuffers), H("glGenFramebuffers", g_GenFramebuffers),
    H("glGenRenderbuffers", g_GenRenderbuffers), H("glGenTextures", g_GenTextures),
    H("glGetActiveUniform", g_GetActiveUniform), H("glGetAttribLocation", g_GetAttribLocation),
    H("glGetIntegerv", g_GetIntegerv), H("glGetProgramInfoLog", g_GetProgramInfoLog),
    H("glGetProgramiv", g_GetProgramiv), H("glGetShaderInfoLog", g_GetShaderInfoLog),
    H("glGetShaderiv", g_GetShaderiv), H("glGetString", g_GetString),
    H("glGetUniformLocation", g_GetUniformLocation), H("glGetUniformfv", g_GetUniformfv),
    H("glLinkProgram", g_LinkProgram), H("glPixelStorei", g_PixelStorei), H("glReadPixels", g_ReadPixels),
    H("glRenderbufferStorage", g_RenderbufferStorage), H("glScissor", g_Scissor),
    H("glShaderSource", g_ShaderSource), H("glTexImage2D", g_TexImage2D), H("glTexParameteri", g_TexParameteri),
    H("glTexSubImage2D", g_TexSubImage2D), H("glUniform1f", g_Uniform1f), H("glUniform1i", g_Uniform1i),
    H("glUniform4f", g_Uniform4f), H("glUniformMatrix4fv", g_UniformMatrix4fv), H("glUseProgram", g_UseProgram),
    H("glValidateProgram", g_ValidateProgram), H("glVertexAttribPointer", g_VertexAttribPointer),
    H("glViewport", g_Viewport),
};

}  // namespace

void hle::register_gles() { add_defs(kDefs, std::size(kDefs)); }
