#!/usr/bin/env python3
"""Check that every public repub_* function declared in repub.h is exported."""
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
header = (root / "include/rolling_epub/repub.h").read_text(encoding="utf-8")
# Match function declarations that start a line with repub_ or a pointer thereof.
names = set()
for line in header.splitlines():
    line = line.strip()
    if line.startswith("/*") or line.startswith("*") or line.startswith("//"):
        continue
    m = re.match(r"^[A-Za-z_][A-Za-z0-9_ \t\*]*?\b(repub_[a-z0-9_]+)\s*\(", line)
    if m:
        names.add(m.group(1))
    m = re.match(r"^(repub_[a-z0-9_]+)\s*\(", line)
    if m:
        names.add(m.group(1))
# Remove declarations that are typedefs or macros seen by the regex.
names.discard("repub_err_t")
names = {n for n in names if not n.startswith("repub_feature_") or n == "repub_feature_enabled"}

nm = subprocess.run(["nm", "-C", "--defined-only", str(root / "build/librolling_epub.a")],
                    capture_output=True, text=True, check=False).stdout
exported = set(re.findall(r"\bT (repub_[a-z0-9_]+)$", nm, re.MULTILINE))
missing = sorted(n for n in names if n not in exported)
extra = sorted(n for n in exported if n not in names)
print(f"declared: {len(names)} exported: {len(exported)}")
if missing:
    print("MISSING EXPORTS:")
    for n in missing:
        print("  ", n)
if extra:
    print("extra symbols (not parsed from header):")
    for n in extra:
        print("  ", n)
sys.exit(1 if missing else 0)
