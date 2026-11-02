#!/usr/bin/env python3
"""Build a conservative guest-library dependency plan from ELF DT_NEEDED.

This is a planning/diagnostic tool, not a runtime loader. System libraries listed
in HOST_LIBS are expected to be supplied by Android/HLE/forwarding, not mapped as
ARM32 guest ELF modules by this reference bridge.
"""
import argparse, re, subprocess
from pathlib import Path

HOST_LIBS = {
    'libandroid.so', 'libEGL.so', 'libGLESv2.so', 'libjnigraphics.so',
    'libOpenSLES.so', 'liblog.so', 'libstdc++.so', 'libm.so', 'libc.so', 'libdl.so'
}

def deps(path):
    p = subprocess.run(['readelf', '-d', str(path)], capture_output=True, text=True, check=True)
    return re.findall(r'\(NEEDED\).*Shared library: \[(.*?)\]', p.stdout)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', required=True, type=Path, help='directory containing extracted ARM32 .so files')
    ap.add_argument('--root', default='libABK.so')
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    libs = {p.name: p for p in args.dir.glob('*.so')}
    graph = {name: deps(path) for name, path in libs.items()}
    order, state, missing, host = [], {}, set(), set()
    cycles = []
    def visit(name, stack):
        s = state.get(name, 0)
        if s == 2: return
        if s == 1:
            cycles.append(' -> '.join(stack + [name])); return
        state[name] = 1
        for dep in graph.get(name, []):
            if dep in libs:
                visit(dep, stack + [name])
            elif dep in HOST_LIBS:
                host.add(dep)
            else:
                missing.add(dep)
        state[name] = 2
        order.append(name)
    if args.root not in libs:
        raise SystemExit(f'root library {args.root!r} not found in {args.dir}')
    visit(args.root, [])
    lines = [f'Root: {args.root}', '', 'Guest libraries in dependency-first order:']
    lines += [f'  {i+1}. {name}' for i, name in enumerate(order)]
    lines += ['', 'Android host/HLE libraries referenced:'] + [f'  {x}' for x in sorted(host)]
    lines += ['', 'Dependencies not present in extracted guest set and not in host allowlist:']
    lines += [f'  {x}' for x in sorted(missing)] or ['  (none)']
    lines += ['', 'Cycles:'] + [f'  {x}' for x in cycles] or ['  (none detected)']
    lines += ['', 'Notes:', '  - This is a static dependency plan, not proof that symbols or relocations resolve.', '  - Host library names require implemented forwarding/HLE functions; listing a host library does not mean it is supported.', '  - The existing bridge loader still needs integration for recursive loading, cross-module symbol resolution, and constructor ordering.']
    report = '\n'.join(lines) + '\n'
    if args.out: args.out.write_text(report)
    print(report, end='')

if __name__ == '__main__': main()
