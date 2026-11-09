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
    if count == 0: errors.append('natives_gen.inc has no native trampolines')
    if 'com/exient/' not in txt: errors.append('Exient surface native methods not represented')
    if 'com/rovio/angrybirdsgo/' not in txt and 'com/rovio/skynest/' not in txt:
        errors.append('Rovio native methods not represented')
    print(f'Go-specific JNI trampolines: {count}')
cmake=(root/'CMakeLists.txt').read_text() if (root/'CMakeLists.txt').exists() else ''
if 'OUTPUT_NAME "ABK"' not in cmake: errors.append('ARM64 bridge output must be named libABK.so for the original manifest')
for name in ('gnustl_shared','SkynestSDK','adcolony','js'):
    if f'OUTPUT_NAME "{name}"' not in cmake: errors.append(f'missing ARM64 Java load stub target: {name}')
if not (root/'bridge_src'/'load_stub.cpp').is_file(): errors.append('missing ARM64 Java load-stub source')
if not (root/'scripts'/'package_original_apk.sh').is_file(): errors.append('missing original APK packaging script')
if errors:
    print('BUILD INPUT VALIDATION FAILED')
    print('\n'.join('- '+e for e in errors)); sys.exit(1)
print('BUILD INPUT VALIDATION PASSED')
print('generated includes: present')
print('CMake target: arm64-v8a Android shared library (Dynarmic source required)')
