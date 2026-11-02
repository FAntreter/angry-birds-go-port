#!/usr/bin/env python3
"""Stage ARM32 guest shared libraries under the filenames bridge.cpp expects.

This only prepares native guest payload files. It does not build an Android APK,
copy assets, or produce the ARM64 host bridge.
"""
import argparse
import shutil
import struct
import sys
from pathlib import Path

# guest SONAME -> source file in target_libs; output names match bridge.cpp.
REQUIRED = {
    "libABK.so": "libABK_arm32.so",
    "libgnustl_shared.so": "libgnustl_shared_arm32.so",
    "libSkynestSDK.so": "libSkynestSDK_arm32.so",
}
OPTIONAL = {
    "libadcolony.so": "libadcolony_arm32.so",
    "libjs.so": "libjs_arm32.so",
}


def check_arm32_elf(path: Path) -> None:
    with path.open('rb') as f:
        h = f.read(20)
    if len(h) < 20 or h[:4] != b'\x7fELF':
        raise ValueError(f"{path.name}: not an ELF file")
    if h[4] != 1:
        raise ValueError(f"{path.name}: expected ELFCLASS32, got EI_CLASS={h[4]}")
    if h[5] != 1:
        raise ValueError(f"{path.name}: expected little-endian ELF")
    machine = struct.unpack_from('<H', h, 18)[0]
    if machine != 40:
        raise ValueError(f"{path.name}: expected EM_ARM (40), got e_machine={machine}")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'target_libs')
    p.add_argument('--out', type=Path, required=True, help='directory to receive renamed ARM32 .so files')
    p.add_argument('--include-optional', action='store_true', help='also stage AdColony and JS libs')
    args = p.parse_args()
    mapping = dict(REQUIRED)
    if args.include_optional:
        mapping.update(OPTIONAL)
    missing = [src for src in mapping if not (args.source / src).is_file()]
    if missing:
        print('ERROR: missing source libraries: ' + ', '.join(missing), file=sys.stderr)
        return 2
    try:
        for src_name, out_name in mapping.items():
            src = args.source / src_name
            check_arm32_elf(src)
            args.out.mkdir(parents=True, exist_ok=True)
            dst = args.out / out_name
            shutil.copy2(src, dst)
            print(f'{src_name} -> {dst}')
    except (OSError, ValueError) as e:
        print(f'ERROR: {e}', file=sys.stderr)
        return 2
    print(f'Staged {len(mapping)} ARM32 guest libraries. This is not an APK build.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
