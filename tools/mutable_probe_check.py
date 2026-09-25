"""Check a -mutableprobe / -dx12mutablesmoke screenshot.

The probe (RS2MutableProbe.cpp) logs the expected colour of known texels as

    RS2MUTABLEPROBE|expect|<name>|<x>|<y>|<r>|<g>|<b>|<tolerance>

and rectangles to compare with another capture as

    RS2MUTABLEPROBE|cell|<name>|<x>|<y>|<w>|<h>

  (default)  Compare each expected pixel.  A Direct3D 8 capture passing here
             is what shows the expectations are right.
  --reference
             Also compare the cells with another capture (Direct3D 8 as the
             reference).  The texture cells are compared pixel for pixel.  The
             text cells are compared by what a systematic error would change:
             the bounding box of the text pixels (position and size) and how
             many pixels are text.  Glyph edges may differ by rasterisation;
             an offset of the box, a changed size or a large change in the
             pixel count is a failure.

Usage:
    python tools/mutable_probe_check.py probe.png probe.log [--reference d3d8.png]
"""

import argparse
import re
import sys

from PIL import Image

BACKGROUND = (16, 32, 48)
TEXT_CELLS = ('strings', 'livetext')


def origin(image):
    for y in range(image.height):
        for x in range(image.width):
            if image.getpixel((x, y)) == BACKGROUND:
                return x, y
    raise SystemExit('FAIL: clear colour not found')


def text_box(image, ox, oy, x, y, w, h):
    px = image.load()
    x0, y0, x1, y1, count = w, h, -1, -1, 0
    for yy in range(h):
        for xx in range(w):
            p = px[ox + x + xx, oy + y + yy]
            if max(abs(a - b) for a, b in zip(p, BACKGROUND)) > 24:
                count += 1
                x0, y0, x1, y1 = min(x0, xx), min(y0, yy), max(x1, xx), max(y1, yy)
    return (x0, y0, x1, y1), count


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('screenshot')
    parser.add_argument('log')
    parser.add_argument('--reference')
    parser.add_argument('--tolerance', type=int, default=2)
    args = parser.parse_args()

    expected, cells = [], []
    for line in open(args.log, encoding='utf-8', errors='replace'):
        m = re.search(r'RS2MUTABLEPROBE\|expect\|([^|]+)\|(-?\d+)\|(-?\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)', line)
        if m:
            expected.append((m.group(1), int(m.group(2)), int(m.group(3)),
                             (int(m.group(4)), int(m.group(5)), int(m.group(6))), int(m.group(7))))
        m = re.search(r'RS2MUTABLEPROBE\|cell\|([^|]+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)', line)
        if m:
            cells.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))))
    if not expected:
        raise SystemExit('FAIL: no expectations in %s' % args.log)

    image = Image.open(args.screenshot).convert('RGB')
    ox, oy = origin(image)
    failures = 0
    for name, x, y, rgb, tolerance in expected:
        actual = image.getpixel((ox + x, oy + y))
        delta = max(abs(a - b) for a, b in zip(actual, rgb))
        if delta > tolerance:
            failures += 1
            print('%-22s at (%3d,%3d) expected %-15s actual %-15s delta %d FAIL' % (
                name, x, y, rgb, actual, delta))
    print('mutable probe: %d/%d pixel expectations; client origin=%s' % (
        len(expected) - failures, len(expected), (ox, oy)))

    if args.reference:
        ref = Image.open(args.reference).convert('RGB')
        rx, ry = origin(ref)
        bad = 0
        for name, x, y, w, h in cells:
            if name in TEXT_CELLS:
                (a, ca), (b, cb) = text_box(image, ox, oy, x, y, w, h), text_box(ref, rx, ry, x, y, w, h)
                shift = (a[0] - b[0], a[1] - b[1])
                size = (a[2] - a[0] - (b[2] - b[0]), a[3] - a[1] - (b[3] - b[1]))
                ratio = ca / cb if cb else 0.0
                ok = cb > 0 and shift == (0, 0) and size == (0, 0) and 0.85 <= ratio <= 1.15
                print('%-16s text box %s vs %s: shift %s, size %s, pixels %d vs %d (x%.2f) %s' % (
                    name, a, b, shift, size, ca, cb, ratio, 'ok' if ok else 'FAIL'))
            else:
                worst, differing = 0, 0
                for yy in range(y, y + h):
                    for xx in range(x, x + w):
                        d = max(abs(p - q) for p, q in zip(image.getpixel((ox + xx, oy + yy)),
                                                            ref.getpixel((rx + xx, ry + yy))))
                        worst = max(worst, d)
                        differing += d > args.tolerance
                ok = differing == 0
                print('%-16s max delta %3d, %5d pixel(s) beyond %d %s' % (
                    name, worst, differing, args.tolerance, 'ok' if ok else 'FAIL'))
            bad += 0 if ok else 1
        print('reference: %d/%d cells match' % (len(cells) - bad, len(cells)))
        failures += bad

    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
