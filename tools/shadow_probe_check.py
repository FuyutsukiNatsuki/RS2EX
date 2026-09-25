"""Check a -shadowprobe screenshot.

The probe (RS2ShadowProbe.cpp) logs the expected colour of every region as

    RS2SHADOWPROBE|expect|<name>|<x>|<y>|<r>|<g>|<b>|<tolerance>

in client-area coordinates, and the rectangle of every cell as

    RS2SHADOWPROBE|cell|<n>|<name>|x=|y=|w=|h=

  (default)  Compare each expected pixel.  A Direct3D 8 capture passing here
             is what shows the expectations are right.
  --reference
             Also compare every cell, pixel for pixel, with another capture -
             Direct3D 8 as the reference, Direct3D 12 as the candidate - and
             report how many pixels of each cell differ by more than
             --tolerance.  Stencil edges fall on the same pixels when both
             renderers rasterise the same quads, so any difference fails.

Usage:
    python tools/shadow_probe_check.py probe.png probe.log [--reference d3d8.png]
"""

import argparse
import re
import sys

from PIL import Image

BACKGROUND = (16, 32, 48)


def origin(image):
    for y in range(image.height):
        for x in range(image.width):
            if image.getpixel((x, y)) == BACKGROUND:
                return x, y
    raise SystemExit('FAIL: clear colour not found')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('screenshot')
    parser.add_argument('log')
    parser.add_argument('--reference')
    parser.add_argument('--tolerance', type=int, default=2)
    args = parser.parse_args()

    expected, cells = [], []
    for line in open(args.log, encoding='utf-8', errors='replace'):
        m = re.search(r'RS2SHADOWPROBE\|expect\|([^|]+)\|(-?\d+)\|(-?\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)', line)
        if m:
            expected.append((m.group(1), int(m.group(2)), int(m.group(3)),
                             (int(m.group(4)), int(m.group(5)), int(m.group(6))), int(m.group(7))))
        m = re.search(r'RS2SHADOWPROBE\|cell\|(\d+)\|([^|]+)\|x=(\d+)\|y=(\d+)\|w=(\d+)\|h=(\d+)', line)
        if m:
            cells.append((m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))))
    if not expected:
        raise SystemExit('FAIL: no expectations in %s' % args.log)

    image = Image.open(args.screenshot).convert('RGB')
    ox, oy = origin(image)
    failures = 0
    for name, x, y, rgb, tolerance in expected:
        actual = image.getpixel((ox + x, oy + y))
        delta = max(abs(a - b) for a, b in zip(actual, rgb))
        ok = delta <= tolerance
        failures += 0 if ok else 1
        if not ok:
            print('%-22s at (%3d,%3d) expected %-15s actual %-15s delta %d FAIL' % (
                name, x, y, rgb, actual, delta))
    print('shadow probe: %d/%d pixel expectations; client origin=%s' % (
        len(expected) - failures, len(expected), (ox, oy)))

    if args.reference:
        ref = Image.open(args.reference).convert('RGB')
        rx, ry = origin(ref)
        bad_cells = 0
        for name, x, y, w, h in cells:
            worst, differing = 0, 0
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    a = image.getpixel((ox + xx, oy + yy))
                    b = ref.getpixel((rx + xx, ry + yy))
                    d = max(abs(p - q) for p, q in zip(a, b))
                    worst = max(worst, d)
                    differing += 1 if d > args.tolerance else 0
            ok = differing == 0
            bad_cells += 0 if ok else 1
            print('%-18s max delta %3d, %5d pixel(s) beyond %d %s' % (
                name, worst, differing, args.tolerance, 'ok' if ok else 'FAIL'))
        print('reference: %d/%d cells match' % (len(cells) - bad_cells, len(cells)))
        failures += bad_cells

    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
