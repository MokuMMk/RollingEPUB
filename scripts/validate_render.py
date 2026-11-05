#!/usr/bin/env python3
"""Validate RollingEPUB rendered PNGs: dimensions and non-blank content."""
import glob
import os
import sys

from PIL import Image


def main(out_dir: str) -> int:
    bad = 0
    paths = sorted(glob.glob(os.path.join(out_dir, "*.png")))
    if not paths:
        print("no PNG files found", file=sys.stderr)
        return 1
    for path in paths:
        im = Image.open(path).convert("L")
        lo, hi = im.getextrema()
        dark = sum(im.histogram()[:128])
        name = os.path.basename(path)
        print(f"{name:28s} {im.size[0]}x{im.size[1]} range={lo}..{hi} dark_px={dark}")
        if not (name.startswith("cover") or name.startswith("image")):
            if im.size != (480, 800):
                print(f"  WARN: unexpected page size for {name}")
        if dark == 0:
            print(f"  FAIL: {name} is blank")
            bad += 1
    print("SMOKE", "PASS" if bad == 0 else "FAIL")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/smoke"))
