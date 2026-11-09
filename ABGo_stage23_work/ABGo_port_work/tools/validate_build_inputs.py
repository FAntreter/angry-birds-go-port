#!/usr/bin/env python3
"""Validate required generated sources and Go-specific JNI trampoline inventory."""
from pathlib import Path
import re, sys
root=Path(__file__).resolve().parents[1]
errors=[]
for name in ('guest_rt_blob.inc','kuser_blob.inc','natives_gen.inc'):
    p=root/'bridge_src'/name
    if not p.is_file() or p.stat().st_size == 0: errors.append(f'missing/empty generated include: {p}')
cm=root/'CMakeLists.txt'
if not cm.is_file(): errors.append('missing root CMakeLists.txt')
else:
    txt=cm.read_text()
    for required in ('bridge_src/gheap.c','bridge_src/bridge.cpp','dynarmic','GLESv2','OpenSLES'):
        if required not in txt: errors.append(f'CMakeLists.txt missing {required}')
native=root/'bridge_src'/'natives_gen.inc'
if native.exists():
    txt=native.read_text()
    count=len(re.findall(r'^\{"[^\n]+native_tramp<',txt,re.M))
    if count != 120: errors.append(f'expected 120 Go native trampolines, found {count}')
    if 'com/rovio/angrybirdsgo/AngryBirdsGoActivity' not in txt: errors.append('Go Activity native methods not represented')
    if 'com/exient/ExientGLSurfaceView' not in txt: errors.append('Exient surface native methods not represented')
if errors:
    print('BUILD INPUT VALIDATION FAILED')
    print('\n'.join('- '+e for e in errors)); sys.exit(1)
print('BUILD INPUT VALIDATION PASSED')
print('generated includes: present')
print('Go-specific JNI trampolines: 120')
print('CMake target: arm64-v8a Android shared library (Dynarmic source required)')
