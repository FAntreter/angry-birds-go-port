#!/usr/bin/env python3
"""Static resolution audit for ARM32 guest imports vs guest exports and HLE names.

This is conservative triage: it does not emulate ELF lookup scopes, symbol versions,
weak-binding corner cases, or prove ABI compatibility of a same-named wrapper.
"""
from pathlib import Path
import re, subprocess

ROOT = Path(__file__).resolve().parents[1]
LIBS = sorted((ROOT / 'target_libs').glob('*.so'))
if not LIBS:
    raise SystemExit('No target ELF files found')

def symbols(path):
    out = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(path)], text=True, errors='replace')
    und, defined = set(), set()
    for line in out.splitlines():
        m = re.match(r'\s*\d+:\s+\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(\S+)\s+(\S+)', line)
        if not m:
            continue
        ndx, name = m.groups()
        name = name.split('@', 1)[0]
        if not name or name == 'UND':
            continue
        (und if ndx == 'UND' else defined).add(name)
    return und, defined

def relocations(path):
    out = subprocess.check_output(['readelf', '-rW', str(path)], text=True, errors='replace')
    found = {}
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 5 and fields[2].startswith('R_ARM_'):
            name = fields[-1].split('@', 1)[0]
            if name and name != '*ABS*':
                found.setdefault(name, []).append(fields[2])
    return found

source = '\n'.join(p.read_text(errors='ignore') for p in (ROOT / 'bridge_src').glob('*') if p.is_file())
# Function registrations use H/HE/HRAW; data imports are registered separately.
# Include all three macros and add_data() so the audit does not mislabel errno-
# aware wrappers or guest data symbols (for example OpenSL ES IIDs) as missing.
hle = set(re.findall(r'\b(?:H|HE|HRAW)\("([^"\n]+)"\s*,', source))
hle.update(re.findall(r'\badd_data\("([^"\n]+)"\s*,', source))
hle.update(re.findall(r'\badd_table_pointer\("([^"\n]+)"\s*,', source))
# OpenSL ES IIDs are registered through names[] then add_data(names[i], ...).
opensles = (ROOT / 'bridge_src' / 'hle_opensles.cpp').read_text(errors='ignore')
iid_table = re.search(r'const char\*\s+names\[\]\s*=\s*\{(.*?)\};', opensles, re.S)
if iid_table:
    hle.update(re.findall(r'"([^"\n]+)"', iid_table.group(1)))
libsyms = {p.name: symbols(p) for p in LIBS}
librelocs = {p.name: relocations(p) for p in LIBS}
all_guest_exports = set().union(*(defined for _, defined in libsyms.values()))
rows=[]
for p in LIBS:
    und, _ = libsyms[p.name]
    for sym in sorted(und):
        if sym in hle:
            state='HLE name present (ABI unverified)'
        elif sym in all_guest_exports:
            state='exported by another bundled guest ELF (scope/order unverified)'
        else:
            state='no matching guest export or HLE name'
        rows.append((p.name, sym, state))

from collections import Counter
counts=Counter(state for _,_,state in rows)
critical=[r for r in rows if r[2]=='no matching guest export or HLE name']
report=['# Stage 9 — guest import resolution audit','',
        'Generated from `readelf --dyn-syms` for bundled ARM32 libraries and HLE registration names found in bridge source.','',
        '**Important:** a name match is not proof of ABI correctness or runtime resolution. This audit does not account for symbol versions, visibility, exact ELF lookup scopes, or symbols obtained dynamically.','',
        '## Summary','',
        f'- Undefined symbol names inspected: **{len(rows)}** (counted per library/import occurrence).',
        f'- HLE-name matches: **{counts["HLE name present (ABI unverified)"]}**.',
        f'- Names exported by another bundled guest ELF: **{counts["exported by another bundled guest ELF (scope/order unverified)"]}**.',
        f'- Names with no matching guest export/HLE name: **{counts["no matching guest export or HLE name"]}**.',
        f'- Unmatched names with dynamic relocations: **{sum(bool(librelocs[lib].get(sym)) for lib, sym, _ in critical)}**.',
        f'- Unmatched names without dynamic relocations: **{sum(not librelocs[lib].get(sym) for lib, sym, _ in critical)}**.', '',
        '## Unmatched imports by library','']
for p in LIBS:
    names=[s for lib,s,state in critical if lib==p.name]
    report.append(f'### `{p.name}` — {len(names)} unmatched names')
    if names:
        for s in names:
            kinds = sorted(set(librelocs[p.name].get(s, [])))
            detail = ', '.join(kinds) if kinds else 'no dynamic relocation (possibly dlsym/dead import)'
            report.append(f'- `{s}` — {detail}')
    else:
        report.append('No unmatched names in this static comparison.')
    report.append('')
report += ['## Interpretation and next actions','',
           '1. The OpenSL ES IID names and bridge registrations are included in this audit; interface/vtable behavior still needs runtime validation.',
           '2. Remaining unmatched names include libc, pthread, networking, process-control, and stdio symbols. Implement only functions with correct guest ABI and semantics; do not install generic no-op stubs for process or socket APIs.',
           '3. Some names may be provided dynamically or through symbol-versioned dependencies; inspect ELF version tables and actual loader lookup scopes before treating every unmatched name as a fatal gap.',
           '4. Runtime loader diagnostics now report the importing module, symbol, and relocation type for unresolved imports; device validation is still required.',
           '5. NDK compilation and static checks do not establish launchability. Install/launch and gameplay validation on the target Android device remain required.', '',
           '## Re-run', '`python3 tools/audit_guest_import_resolution.py`', '']
path=ROOT/'reports'/'stage9_guest_import_resolution.md'
path.write_text('\n'.join(report))
print(f'Wrote {path.relative_to(ROOT)}')
for k in ['HLE name present (ABI unverified)','exported by another bundled guest ELF (scope/order unverified)','no matching guest export or HLE name']:
    print(f'{k}: {counts[k]}')
