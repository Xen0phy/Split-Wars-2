#!/usr/bin/env python3
"""
bump_rev.py  —  bumps Rev in version.h on every build.
Skips if Rev is already 0 (i.e. you just manually reset it).
Run from project root.
"""

import re
from pathlib import Path

VERSION_FILE = Path("src/addon/version.h")

text = VERSION_FILE.read_text(encoding="utf-8")

def get(name):
    m = re.search(rf"const int {name}\s*=\s*(-?\d+)", text)
    return int(m.group(1)) if m else 0

maj, min_, bld, rev = get("Maj"), get("Min"), get("Bld"), get("Rev")

rev += 1
new_text = re.sub(r"(const int Rev\s*=\s*)-?\d+", rf"\g<1>{rev}", text)
VERSION_FILE.write_text(new_text, encoding="utf-8")
print(f"[bump_rev] {maj}.{min_}.{bld}.{rev}")