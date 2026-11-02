#!/usr/bin/env python3
"""Report ARM32 dynamic relocation types in bundled guest ELFs vs loader support."""
from pathlib import Path
import re, subprocess, collections
ROOT = Path(__file__).resolve().parents[1]
LIBS = sorted((ROOT / 'target_libs').glob('*.so'))
if not LIBS:
    raise SystemExit('No target ELF files found')
# elf32.cpp currently supports only these relocation numbers.
SUPPORTED = {0: 'R_ARM_NONE', 2: 'R_ARM_ABS32', 3: 'R_ARM_REL32', 21: 'R_ARM_GLOB_DAT', 22: 'R_ARM_JUMP_SLOT', 23: 'R_ARM_RELATIVE'}
all_rows=[]
for lib in LIBS:
    out=subprocess.run(['readelf','-rW',str(lib)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True).stdout
    counts=collections.Counter()
    for line in out.splitlines():
        m=re.match(r'\s*[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+(R_ARM_[A-Z0-9_]+)',line)
        if m:
            typ=m.group(1)
            counts[typ]+=1
    for typ,n in sorted(counts.items()):
        all_rows.append((lib.name,typ,n,typ in SUPPORTED.values()))
report=['# Stage 11 — ARM32 relocation compatibility audit','',
        'This report compares relocation names found by `readelf -rW` with the relocation cases currently implemented in `bridge_src/elf32.cpp`. It is static analysis, not a build or runtime test.','',
        '## Per-library relocation inventory','']
for lib in LIBS:
    rows=[r for r in all_rows if r[0]==lib.name]
    report.append(f'### `{lib.name}`')
    if not rows:
        report.append('No named dynamic relocations parsed (verify ELF relocation tables manually).')
    else:
        for _,typ,n,supported in rows:
            report.append(f'- `{typ}`: {n} — {"supported by current loader" if supported else "NOT supported by current loader"}')
    report.append('')
unsupported=[r for r in all_rows if not r[3]]
report += ['## Summary', '',
           f'- Relocation types observed: **{len(set(r[1] for r in all_rows))}**.',
           f'- Relocation entries whose type is not recognized by the current loader: **{sum(r[2] for r in unsupported)}**.',
           '- The parser may not see every relocation format if a library uses RELA, RELR, Android packed relocations, or a toolchain-specific format.', '',
           '## Current supported set', ', '.join(f'`{name}`' for name in SUPPORTED.values()), '',
           '## Important limitations',
           '- A supported relocation type does not prove symbol lookup order, addend handling, weak-symbol semantics, or relocation target bounds are correct.',
           '- Unsupported relocation types are fatal in the current loader; do not treat an import audit alone as evidence that loading will succeed.',
           '- Unresolved import messages now identify the importing guest library and explicitly state when a diagnostic thunk is installed. Such thunks are not real implementations.', '',
           '## Re-run', '`python3 tools/audit_elf_relocations.py`', '']
path=ROOT/'reports'/'stage11_relocation_compatibility.md'
path.write_text('\n'.join(report))
print(f'Wrote {path.relative_to(ROOT)}')
print(f'Observed {len(set(r[1] for r in all_rows))} relocation types; unsupported entries: {sum(r[2] for r in unsupported)}')
for lib in LIBS:
    bad=[r for r in unsupported if r[0]==lib.name]
    if bad: print(lib.name+': '+', '.join(f'{typ}={n}' for _,typ,n,_ in bad))
