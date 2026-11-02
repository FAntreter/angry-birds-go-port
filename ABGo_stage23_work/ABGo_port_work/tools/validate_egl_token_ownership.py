#!/usr/bin/env python3
"""Static checks that EGL operations reject cross-display/stale guest tokens."""
from pathlib import Path
s = (Path(__file__).resolve().parents[1] / 'bridge_src/hle_egl.cpp').read_text()
assert 'template<class T> bool token_belongs_to(uint32_t id, uint32_t display)' in s
assert 'o->second == display' in s
assert 'template<class T> T get_owned(uint32_t id, uint32_t display)' in s
checks = {
    'eglGetConfigAttrib': 'token_belongs_to<EGLConfig>(c,d)',
    'eglCreateContext config': 'token_belongs_to<EGLConfig>(c,d)',
    'eglCreateContext share context': 'token_belongs_to<EGLContext>(share,d)',
    'eglDestroyContext': 'token_belongs_to<EGLContext>(c,d)',
    'eglCreateWindowSurface': 'token_belongs_to<EGLConfig>(c,d)',
    'eglDestroySurface': 'token_belongs_to<EGLSurface>(s,d)',
    'eglMakeCurrent draw surface': 'token_belongs_to<EGLSurface>(draw,d)',
    'eglMakeCurrent read surface': 'token_belongs_to<EGLSurface>(read,d)',
    'eglMakeCurrent context': 'token_belongs_to<EGLContext>(ctx,d)',
    'eglSwapBuffers': 'token_belongs_to<EGLSurface>(s,d)',
    'eglSurfaceAttrib': 'token_belongs_to<EGLSurface>(s,d)',
    'eglQuerySurface': 'token_belongs_to<EGLSurface>(s,d)',
}
missing = [label for label, needle in checks.items() if needle not in s]
assert not missing, 'cross-display/stale-token guards missing: ' + ', '.join(missing)
assert 'if (!s || !token_belongs_to<EGLSurface>(s,d)) return EGL_FALSE;' in s
print(f'PASS: {len(checks)} EGL resource-operation ownership guards present')
