#!/usr/bin/env python3
"""Compare an original APK's DEX native declarations with natives_gen.inc.

Run from Codespaces after placing the original APK in the workspace:
  python3 tools/audit_dex_native_parity.py /path/to/angry-birds-go-2-9-1.apk
"""
from __future__ import annotations
import re
import struct
import sys
import zipfile
from pathlib import Path

ACC_STATIC = 0x8
ACC_NATIVE = 0x100

def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]

def uleb(data: bytes, pos: int) -> tuple[int, int]:
    value = shift = 0
    while True:
        b = data[pos]
        pos += 1
        value |= (b & 0x7f) << shift
        if not b & 0x80:
            return value, pos
        shift += 7
        if shift > 35:
            raise ValueError("invalid ULEB128")

def dex_strings(data: bytes) -> list[str]:
    size, off = u32(data, 56), u32(data, 60)
    out = []
    for i in range(size):
        item = u32(data, off + 4 * i)
        _, p = uleb(data, item)  # UTF-16 length; strings here are ASCII-compatible
        end = data.index(b"\0", p)
        out.append(data[p:end].decode("utf-8", "replace"))
    return out

def dex_native_methods(data: bytes) -> dict[tuple[str, str, str], bool]:
    if data[:4] != b"dex\\n":
        raise ValueError("not a DEX file")
    strings = dex_strings(data)
    type_size, type_off = u32(data, 64), u32(data, 68)
    types = [strings[u32(data, type_off + 4*i)] for i in range(type_size)]

    proto_size, proto_off = u32(data, 72), u32(data, 76)
    protos = []
    for i in range(proto_size):
        off = proto_off + 12*i
        ret_idx, params_off = u32(data, off + 4), u32(data, off + 8)
        params = []
        if params_off:
            n = u32(data, params_off)
            params = [types[struct.unpack_from("<H", data, params_off + 4 + 2*j)[0]]
                      for j in range(n)]
        protos.append("(" + "".join(params) + ")" + types[ret_idx])

    method_size, method_off = u32(data, 88), u32(data, 92)
    methods = []
    for i in range(method_size):
        off = method_off + 8*i
        class_idx, proto_idx, name_idx = struct.unpack_from("<HHI", data, off)
        methods.append((types[class_idx], strings[name_idx], protos[proto_idx]))

    class_size, class_off = u32(data, 96), u32(data, 100)
    found = {}
    for i in range(class_size):
        off = class_off + 32*i
        class_idx = u32(data, off)
        class_data_off = u32(data, off + 24)
        if not class_data_off:
            continue
        p = class_data_off
        static_fields, p = uleb(data, p)
        instance_fields, p = uleb(data, p)
        direct_methods, p = uleb(data, p)
        virtual_methods, p = uleb(data, p)
        for _ in range(static_fields + instance_fields):
            _, p = uleb(data, p)  # field_idx_diff
            _, p = uleb(data, p)  # access_flags
        for count in (direct_methods, virtual_methods):
            method_idx = 0
            for _ in range(count):
                diff, p = uleb(data, p)
                access, p = uleb(data, p)
                _, p = uleb(data, p)  # code_off
                method_idx += diff
                if access & ACC_NATIVE:
                    cls, name, sig = methods[method_idx]
                    key = (cls[1:-1], name, sig)
                    if key in found:
                        raise ValueError(f"duplicate DEX native declaration: {key}")
                    found[key] = bool(access & ACC_STATIC)
    return found

def cpp_abi(sig: str) -> list[str]:
    def one_type(desc: str, i: int) -> tuple[str, int]:
        start = i
        while i < len(desc) and desc[i] == "[":
            i += 1
        if i < len(desc) and desc[i] == "L":
            i = desc.index(";", i) + 1
            return "jobject", i
        if i >= len(desc):
            raise ValueError(f"bad descriptor: {desc}")
        t = desc[i]
        mapping = {"V":"void", "Z":"jboolean", "B":"jbyte", "C":"jchar",
                   "S":"jshort", "I":"jint", "J":"jlong", "F":"jfloat", "D":"jdouble"}
        if start != i:
            return "jobject", i + 1
        return mapping[t], i + 1
    if not sig.startswith("("):
        raise ValueError(f"bad descriptor: {sig}")
    args, i = [], 1
    while sig[i] != ")":
        t, i = one_type(sig, i)
        args.append(t)
    ret, end = one_type(sig, i + 1)
    if end != len(sig):
        raise ValueError(f"trailing descriptor data: {sig}")
    return [ret] + args

def read_table(path: Path):
    rows = {}
    duplicates = []
    for line in path.read_text().splitlines():
        if not line.startswith('{"'):
            continue
        m = re.match(r'^\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",.*?native_tramp<(\d+),\s*([^>]+)>', line)
        if not m:
            raise ValueError(f"cannot parse generated row: {line[:180]}")
        cls, name, sig, index, types = m.groups()
        key = (cls, name, sig)
        if key in rows:
            duplicates.append(key)
        rows[key] = (int(index), [x.strip() for x in types.split(",")])
    return rows, duplicates

def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    apk = Path(sys.argv[1])
    root = Path(__file__).resolve().parents[1]
    table_path = root / "bridge_src" / "natives_gen.inc"
    with zipfile.ZipFile(apk) as z:
        dex_names = sorted(n for n in z.namelist()
                           if re.fullmatch(r"classes(?:\d+)?\.dex", n))
        dex = {}
        for name in dex_names:
            dex.update(dex_native_methods(z.read(name)))
    table, duplicates = read_table(table_path)
    missing = sorted(set(dex) - set(table))
    extra = sorted(set(table) - set(dex))
    abi_bad = []
    for key in sorted(set(dex) & set(table)):
        _, actual = table[key]
        expected = cpp_abi(key[2])
        if actual != expected:
            abi_bad.append((key, expected, actual))
    static_count = sum(dex.values())
    print(f"DEX native declarations: {len(dex)} ({static_count} static, {len(dex)-static_count} instance)")
    print(f"Generated trampoline rows: {len(table)}")
    print(f"Missing rows: {len(missing)}; extra rows: {len(extra)}; duplicate rows: {len(duplicates)}")
    print(f"JNI ABI signature mismatches: {len(abi_bad)}")
    for label, items in (("MISSING", missing), ("EXTRA", extra), ("ABI", abi_bad), ("DUPLICATE", duplicates)):
        for item in items[:40]:
            print(f"{label}: {item}")
    if missing or extra or duplicates or abi_bad or len(dex) != 120 or len(table) != 120:
        return 1
    print("PASS: class/name/descriptor and generated JNI C++ ABI types match exactly.")
    print("Note: JNI static-vs-instance flags are reported above; both use the same pointer-sized JNI receiver ABI.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
