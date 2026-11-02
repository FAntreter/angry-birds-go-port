#!/usr/bin/env python3
"""Static symbol coverage comparison for target ELF imports and bridge HLE registrations."""
import argparse, re, subprocess
from pathlib import Path

HLE_FILES = ['hle_libc.cpp','hle_stdio.cpp','hle_pthread.cpp','hle_gles.cpp','hle_android.cpp']

def undefined_symbols(path):
    out = subprocess.run(['readelf','--dyn-syms','--wide',str(path)],capture_output=True,text=True,check=True).stdout
    result=set()
    for line in out.splitlines():
        # Num: Value Size Type Bind Vis Ndx Name
        m=re.match(r'\s*\d+:\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+UND\s+(\S+)', line)
        if m:
            result.add(m.group(1).split('@')[0])
    return result

def hle_symbols(src):
    found=set()
    for name in HLE_FILES:
        p=src/name
        if not p.exists(): continue
        text=p.read_text(errors='replace')
        found.update(re.findall(r'\bH(?:E|RAW)?\("([^"]+)"\s*,',text))
    return found

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--libs',type=Path,required=True)
    ap.add_argument('--src',type=Path,required=True,help='directory containing bridge source files')
    ap.add_argument('--out',type=Path)
    args=ap.parse_args()
    hle=hle_symbols(args.src)
    lines=['# HLE import coverage (static approximation)','',f'HLE names extracted from registration macros: {len(hle)}','']
    for lib in sorted(args.libs.glob('*.so')):
        und=undefined_symbols(lib)
        matched=sorted(und & hle)
        unmatched=sorted(und - hle)
        lines += [f'## {lib.name}',f'- Unique undefined dynamic symbols: {len(und)}',f'- Name matches in HLE registrations: {len(matched)}',f'- Names without HLE registration match: {len(unmatched)}','', '### Unmatched names']
        lines += [f'- `{s}`' for s in unmatched] or ['- (none)']
        lines += ['', '### HLE name matches']
        lines += [f'- `{s}`' for s in matched] or ['- (none)']
        lines += ['']
    report='\n'.join(lines)+'\n'
    if args.out: args.out.write_text(report)
    print(report,end='')
if __name__=='__main__': main()
