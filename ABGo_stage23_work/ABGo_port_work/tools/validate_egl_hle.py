#!/usr/bin/env python3
"""Static regression check for the initial EGL/native-window HLE boundary."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
source = (root / 'bridge_src/hle_egl.cpp').read_text()
hle_h = (root / 'bridge_src/hle.h').read_text()
hle_cpp = (root / 'bridge_src/hle.cpp').read_text()
cmake = (root / 'CMakeLists.txt').read_text()
required = [
 'eglGetDisplay','eglInitialize','eglTerminate','eglBindAPI','eglChooseConfig','eglGetConfigs',
 'eglGetConfigAttrib','eglCreateContext','eglDestroyContext','eglCreateWindowSurface','eglDestroySurface',
 'eglMakeCurrent','eglSwapBuffers','eglSwapInterval','eglSurfaceAttrib','eglQuerySurface','eglQueryString',
 'eglGetCurrentContext','eglGetCurrentSurface','eglGetProcAddress','eglGetError',
 'ANativeWindow_fromSurface','ANativeWindow_release','ANativeWindow_setBuffersGeometry'
]
missing = [name for name in required if f'"{name}"' not in source]
assert not missing, f'missing EGL/native-window registrations: {missing}'
assert 'void register_egl();' in hle_h
assert 'register_egl();' in hle_cpp
assert 'bridge_src/hle_egl.cpp' in cmake
assert 'raw 64-bit host pointers are never exposed' in source
print(f'PASS: {len(required)} EGL/native-window HLE registrations present; build wiring present')
