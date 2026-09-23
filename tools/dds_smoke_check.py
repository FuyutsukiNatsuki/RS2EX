"""Check a -dx12ddssmoke screenshot against the pixels the smoke expected.

The smoke (RS2D3D12DDSSmoke.cpp) builds its DDS fixtures from single-colour
blocks, so it can compute every expected pixel exactly; it logs each one as

    RS2D3D12DDS|expect|<name>|<x>|<y>|<r>|<g>|<b>|<tolerance>

in client-area coordinates.  This script finds the client area by the clear
colour and compares.  It knows nothing about DDS: if the smoke and this
disagree, the smoke's log is the specification.

Usage:
    python tools/dds_smoke_check.py screenshot.png smoke.log
"""

import argparse
import re
import sys

from PIL import Image

BACKGROUND = (16, 32, 48)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('screenshot')
    parser.add_argument('log')
    args = parser.parse_args()

    expected = []
    for line in open(args.log, encoding='utf-8', errors='replace'):
        m = re.search(r'RS2D3D12DDS\|expect\|([^|]+)\|(-?\d+)\|(-?\d+)\|(\d+)\|(\d+)\|(\d+)\|(\d+)', line)
        if m:
            expected.append((m.group(1), int(m.group(2)), int(m.group(3)),
                             (int(m.group(4)), int(m.group(5)), int(m.group(6))), int(m.group(7))))
    if not expected:
        raise SystemExit('FAIL: no expectations in %s' % args.log)

    image = Image.open(args.screenshot).convert('RGB')
    origin = None
    for y in range(image.height):
        for x in range(image.width):
            if image.getpixel((x, y)) == BACKGROUND:
                origin = (x, y)
                break
        if origin:
            break
    if not origin:
        raise SystemExit('FAIL: clear colour not found')

    failures = 0
    for name, x, y, rgb, tolerance in expected:
        actual = image.getpixel((origin[0] + x, origin[1] + y))
        delta = max(abs(a - b) for a, b in zip(actual, rgb))
        ok = delta <= tolerance
        failures += 0 if ok else 1
        print('%-26s at (%3d,%3d) expected %-15s actual %-15s delta %d %s' % (
            name, x, y, rgb, actual, delta, 'ok' if ok else 'FAIL'))
    print('dds smoke: %d/%d pixel expectations; client origin=%s' % (
        len(expected) - failures, len(expected), origin))
    if failures:
        sys.exit(1)


if __name__ == '__main__':
    main()
