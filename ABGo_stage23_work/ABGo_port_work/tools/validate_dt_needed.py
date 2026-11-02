#!/usr/bin/env python3
"""Compare DT_NEEDED names parsed from ELF32 files with readelf output."""
import re, struct, subprocess, sys
from pathlib import Path

def parse_needed(blob):
    if blob[:4] != b'\x7fELF' or blob[4] != 1 or blob[5] != 1:
        raise ValueError('expected little-endian ELF32')
    e_phoff = struct.unpack_from('<I', blob, 28)[0]
    e_phentsize, e_phnum = struct.unpack_from('<HH', blob, 42)
    phdrs = [struct.unpack_from('<IIIIIIII', blob, e_phoff+i*e_phentsize) for i in range(e_phnum)]
    pt_load, pt_dynamic = 1, 2
    loads = [p for p in phdrs if p[0] == pt_load]
    dyns = [p for p in phdrs if p[0] == pt_dynamic]
    if not dyns: return []
    _, dyn_off, _, _, dyn_filesz, _, _, _ = dyns[0]
    tags = []
    for off in range(dyn_off, dyn_off+dyn_filesz, 8):
        tag, val = struct.unpack_from('<iI', blob, off)
        if tag == 0: break
        tags.append((tag, val))
    str_vaddr = next((v for t,v in tags if t == 5), None) # DT_STRTAB
    strsz = next((v for t,v in tags if t == 10), None) # DT_STRSZ
    offs = [v for t,v in tags if t == 1] # DT_NEEDED
    if not offs: return []
    if str_vaddr is None or strsz is None: raise ValueError('missing DT_STRTAB/DT_STRSZ')
    str_off = None
    for p in loads:
        _, p_off, p_vaddr, _, p_filesz, _, _, _ = p
        if p_vaddr <= str_vaddr and str_vaddr + strsz <= p_vaddr + p_filesz:
            str_off = p_off + (str_vaddr-p_vaddr); break
    if str_off is None: raise ValueError('string table not in file-backed PT_LOAD')
    strings = blob[str_off:str_off+strsz]
    result=[]
    for off in offs:
        if off >= len(strings): raise ValueError('DT_NEEDED offset out of bounds')
        end=strings.find(b'\0', off)
        if end < 0: raise ValueError('unterminated dependency string')
        result.append(strings[off:end].decode('ascii'))
    return result

for name in sys.argv[1:]:
    path=Path(name); actual=parse_needed(path.read_bytes())
    text=subprocess.run(['readelf','-d',str(path)],capture_output=True,text=True,check=True).stdout
    expected=re.findall(r'\(NEEDED\).*Shared library: \[(.*?)\]',text)
    ok=actual==expected
    print(f'{path.name}: {"PASS" if ok else "FAIL"} ({len(actual)} dependencies)')
    if not ok:
        print('  parser:',actual); print('  readelf:',expected)
        raise SystemExit(1)
