#!/usr/bin/env python3
"""Generate JNI trampoline table from the audited Go DEX native-method inventory.
Input JSON is produced by tools/parse_dex_native_methods.py from the target APK.
"""
import argparse, json, re
from pathlib import Path
JNI_TYPES = {"Z":"jboolean","B":"jbyte","C":"jchar","S":"jshort","I":"jint","J":"jlong","F":"jfloat","D":"jdouble","V":"void"}
def split_sig(sig):
    params=[]; i=1
    while sig[i] != ')':
        j=i
        while sig[j]=='[': j+=1
        if sig[j]=='L': j=sig.index(';',j)
        params.append('L' if sig[i] in 'L[' else sig[i]); i=j+1
    r=sig[i+1]
    return params, ('L' if r in 'L[' else r)
def mangle(s):
    out=[]
    for ch in s:
        if ch=='/': out.append('_')
        elif ch=='_': out.append('_1')
        elif ch==';': out.append('_2')
        elif ch=='[': out.append('_3')
        elif ch.isascii() and ch.isalnum(): out.append(ch)
        else: out.append('_0%04x'%ord(ch))
    return ''.join(out)
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('inventory',type=Path); ap.add_argument('out',type=Path); args=ap.parse_args()
    data=json.loads(args.inventory.read_text()); rows=[]; seen=set()
    for item in data['methods']:
        cls=item['class']
        if cls.startswith('L') and cls.endswith(';'): cls=cls[1:-1]
        name=item['name']; sig=item['descriptor']
        key=(cls,name,sig)
        if key in seen: continue
        seen.add(key)
        params,ret=split_sig(sig)
        ctypes=[JNI_TYPES.get(p,'jobject') for p in params]
        rtype=JNI_TYPES.get(ret,'jobject')
        slot=len(rows); targs=', '.join([str(slot),rtype]+ctypes)
        rows.append(f'{{"{cls}", "{name}", "{sig}", "Java_{mangle(cls)}_{mangle(name)}", reinterpret_cast<void*>(&native_tramp<{targs}>)}},')
    args.out.write_text('// Generated from Angry Birds Go! DEX native declarations; do not edit.\n'+'\n'.join(rows)+'\n')
    print(f'{len(rows)} Go native trampolines -> {args.out}')
if __name__=='__main__': main()
