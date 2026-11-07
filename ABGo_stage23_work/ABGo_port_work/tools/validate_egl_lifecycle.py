#!/usr/bin/env python3
"""Static regression check for per-display EGL token cleanup."""
from pathlib import Path
p = Path(__file__).resolve().parents[1] / 'bridge_src' / 'hle_egl.cpp'
s = p.read_text()
start = s.index('uint32_t g_eglTerminate(')
end = s.index('\nuint32_t g_eglBindAPI', start)
body = s[start:end]
required = [
    'display == EGL_NO_DISPLAY',
    'EGLBoolean result = eglTerminate(display)',
    'if (result)',
    'table<EGLDisplay>().erase(d)',
    'erase_owned_by<EGLConfig>(d)',
    'erase_owned_by<EGLContext>(d)',
    'erase_owned_by<EGLSurface>(d)',
]
missing = [x for x in required if x not in body]
if missing:
    raise SystemExit('FAIL: missing EGL lifecycle handling: ' + ', '.join(missing))
for item in ['put_owned(tmp[static_cast<size_t>(i)], d)', 'put_owned(eglCreateContext(', 'owner->second == display']:
    if item not in s:
        raise SystemExit('FAIL: missing display ownership assignment: ' + item)
if 'EGLSurface surface = eglCreateWindowSurface(' not in s or 'put_owned(surface, d)' not in s:
    raise SystemExit('FAIL: window surface is not tagged to its owning display')
if 'table<EGLConfig>().clear()' in body or 'table<EGLContext>().clear()' in body or 'table<EGLSurface>().clear()' in body:
    raise SystemExit('FAIL: terminating one display still clears all EGL resources')
print('PASS: EGLTerminate removes only resources tagged to the terminated display')
