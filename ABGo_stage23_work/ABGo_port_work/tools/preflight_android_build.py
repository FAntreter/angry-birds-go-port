#!/usr/bin/env python3
"""Preflight the Android ARM64 bridge build prerequisites without pretending to build it."""
from pathlib import Path
import os, shutil, sys
ROOT = Path(__file__).resolve().parents[1]
errors=[]; notes=[]
cmake=shutil.which('cmake')
ninja=shutil.which('ninja')
if not cmake: errors.append('cmake not found on PATH')
if not ninja: notes.append('ninja not found; CMake can use another generator')
ndk_candidates=[]
for key in ('ANDROID_NDK_HOME','ANDROID_NDK_ROOT','NDK_HOME'):
    if os.environ.get(key): ndk_candidates.append(Path(os.environ[key]))
for base in (Path('/opt/android-sdk/ndk'), Path('/usr/local/lib/android/sdk/ndk'), Path.home()/'Android/Sdk/ndk'):
    if base.is_dir(): ndk_candidates.extend(p for p in base.iterdir() if p.is_dir())
ndk=next((p for p in ndk_candidates if (p/'build/cmake/android.toolchain.cmake').is_file()), None)
if not ndk: errors.append('Android NDK not found (set ANDROID_NDK_HOME to an installed NDK)')
else: notes.append(f'Android NDK: {ndk}')
dyn=os.environ.get('DYNARMIC_SRC')
dyn_candidates=[Path(dyn)] if dyn else []
if (ROOT/'third_party/dynarmic/CMakeLists.txt').is_file(): dyn_candidates.append(ROOT/'third_party/dynarmic')
dynarmic=next((p for p in dyn_candidates if (p/'CMakeLists.txt').is_file() and (p/'src/dynarmic/interface/A32/a32.h').is_file()), None)
if not dynarmic: errors.append('Dynarmic A32 source checkout not found (set DYNARMIC_SRC to a compatible checkout)')
else: notes.append(f'Dynarmic A32 source: {dynarmic}')
for rel in ('bridge_src/guest_rt_blob.inc','bridge_src/kuser_blob.inc','bridge_src/natives_gen.inc','bridge_src/gheap.c','CMakeLists.txt'):
    p=ROOT/rel
    if not p.is_file() or p.stat().st_size == 0: errors.append(f'missing required build input: {rel}')
if notes:
    print('\n'.join('INFO: '+x for x in notes))
if errors:
    print('ANDROID BUILD PREFLIGHT: BLOCKED')
    print('\n'.join('BLOCKER: '+x for x in errors))
    print('No compilation was attempted because required toolchain inputs are absent.')
    sys.exit(2)
print('ANDROID BUILD PREFLIGHT: READY TO ATTEMPT CONFIGURATION')
print(f'Use: cmake -S "{ROOT}" -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="{ndk}/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 -DDYNARMIC_SRC="{dynarmic}"')
