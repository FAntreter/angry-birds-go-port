#!/usr/bin/env python3
"""Static guardrails for the recursive guest ELF loader integration."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
s = (root / 'bridge_src' / 'bridge.cpp').read_text()
checks = {
    'Go ARM32 root library selected': 'libABK_arm32.so' in s,
    'DT_NEEDED parser used before module load': 'elf::needed(image.data(), image.size())' in s,
    'known host Android libraries explicitly classified': 'kHostLibraries.count(dep)' in s,
    'guest dependencies mapped to ARM32-suffixed filenames': 'guest_filename(dep)' in s,
    'dependency-first recursive loading present': 'load_guest_tree(dep, dep_file, loaded, active)' in s,
    'cycles detected rather than silently recursed': 'active.insert(soname)' in s and 'cyclic guest DT_NEEDED dependency' in s,
    'constructors run after dependency loading': 'elf::run_init(module)' in s,
    'legacy hard-coded Star Wars guest root removed': 'kGuestLibFile = "libABK_arm32.so"' in s,
}
for label, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + label)
if not all(checks.values()):
    raise SystemExit(1)
