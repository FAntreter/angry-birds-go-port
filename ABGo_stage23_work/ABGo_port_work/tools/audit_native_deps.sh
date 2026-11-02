#!/usr/bin/env bash
# Static dependency and undefined-symbol inventory for extracted Android ELF libraries.
set -euo pipefail
if [[ $# -lt 1 ]]; then echo "Usage: $0 directory-with-.so-files" >&2; exit 2; fi
for f in "$1"/*.so; do
  [[ -f "$f" ]] || continue
  echo "===== $(basename "$f") : DT_NEEDED ====="
  readelf -d "$f" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\].*/NEEDED \1/p'
  echo "===== $(basename "$f") : undefined dynamic symbols ====="
  readelf --dyn-syms --wide "$f" 2>/dev/null | awk '$7 == "UND" {print $8}' | sort -u
  echo
 done
