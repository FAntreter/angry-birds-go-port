#!/usr/bin/env python3
"""Inventory update/offline-related strings in the recovered ARM32 game libraries.

This is a discovery aid only: strings do not prove a call path or patch location.
"""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LIBS = [ROOT / "target_libs/libABK.so", ROOT / "target_libs/libSkynestSDK.so"]
PATTERN = re.compile(
    r"update|version|internet|network|connect|connection|offline|download|cloud|"
    r"asset|content|manifest|patch|server|http|https|timeout|retry|unavailable|"
    r"no internet|can't contact|cannot contact|checking",
    re.IGNORECASE,
)

out = ["# Startup update/network string inventory", "",
       "Generated from the checked-in ARM32 libraries. Offsets are file offsets from GNU strings; "
       "a matching string alone does not identify the code that consumes it.", ""]
for lib in LIBS:
    if not lib.is_file():
        raise SystemExit(f"Missing input library: {lib}")
    out += [f"## {lib.name}", ""]
    raw = subprocess.check_output(["strings", "-a", "-t", "x", str(lib)], text=True, errors="replace")
    matches = []
    for line in raw.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) == 2 and PATTERN.search(parts[1]):
            matches.append(f"- `0x{int(parts[0], 16):x}` — `{parts[1][:240]}`")
    out += matches or ["(No matching strings found.)"]
    out.append("")
report = "\n".join(out)
path = ROOT / "reports/update_network_string_inventory.md"
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(report + "\n")
print(report)
