"""Read a -stageprobe screenshot and check it.

The probe (RS2StageProbe.cpp) draws 36 patches, one texture-stage /
environment / UV case each; patch positions come from the run's own log.

  (default)  Print each patch centre.  For patches that sample the
             gradient texture (texel (x, y) = (x, y, 128)) the sampled
             coordinate is decoded and compared with the formula the
             Direct3D 8 run established:
                 environment:  u = 0.5 nx + 0.5 nz,  v = -0.5 ny + 0.5 nz, wrapped
                 stage 0 UV:   (u, v, 1) times the matrix; row 4 ignored

  --reference
             Compare every patch, over its inner 80%, with the same patch in
             another capture - Direct3D 8 as the reference, Direct3D 12 as the
             candidate.  This is the gate.

Usage:
    python tools/stage_probe_check.py probe.png probe.log
    python tools/stage_probe_check.py d3d12.png d3d12.log --reference d3d8.png
"""

import argparse
import math
import re
import sys

from PIL import Image

BACKGROUND = (16, 32, 48)

# Normals of the gradient patches, restated from RS2StageProbe.cpp.
ENV = {
    'env+x': (1, 0, 0), 'env-x': (-1, 0, 0), 'env+y': (0, 1, 0), 'env-y': (0, -1, 0),
    'env+z': (0, 0, 1), 'env-z': (0, 0, -1),
    'env-diagonal': (0.5773503, 0.5773503, -0.5773503),
    'env-diagonal2': (-0.6, 0.3, -0.7416198),
    'env-world-rotated': (0, 1, 0),          # +x rotated 90 degrees about z
    'env-scaled-world': (-0.6, 0.3, -0.7416198),
    'env-no-normal': (0, 0, 0),
}
UV = {  # (u, v) in, matrix rows as (m0, m1, m4, m5, m8, m9)
    'uv-identity': ((0.3, 0.6), (1, 0, 0, 1, 0, 0)),
    'uv-translate': ((0.3, 0.6), (1, 0, 0, 1, 0.25, 0.125)),
    'uv-scale': ((0.3, 0.3), (0.5, 0, 0, 2, 0, 0)),
    'uv-combined': ((0.3, 0.6), (0.5, 0, 0, 0.5, 0.375, 0.0625)),
    'uv-set-while-off': ((0.3, 0.6), (1, 0, 0, 1, 0, 0)),
    'uv-reenable-stored': ((0.3, 0.6), (1, 0, 0, 1, 0.25, 0.125)),
    'uv-fourth-row': ((0.3, 0.6), (1, 0, 0, 1, 0, 0)),
}


def wrap(x):
    return x - math.floor(x)


def env_uv(n):
    length = math.sqrt(sum(c * c for c in n))
    n = tuple(c / length for c in n) if length else n
    return wrap(0.5 * n[0] + 0.5 * n[2]), wrap(-0.5 * n[1] + 0.5 * n[2])


def uv_uv(name):
    (u, v), (m0, m1, m4, m5, m8, m9) = UV[name]
    return wrap(u * m0 + v * m4 + m8), wrap(u * m1 + v * m5 + m9)


def texel(u, v):
    return min(255, int(u * 256)), min(255, int(v * 256))


def read_patches(log):
    patches, viewport = [], None
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.search(r'RS2STAGEPROBE\|begin\|backend=([^|]*)\|viewport=(\d+)x(\d+)', line)
        if m:
            viewport, patches = (int(m.group(2)), int(m.group(3))), []
        m = re.search(r'RS2STAGEPROBE\|patch\|(\d+)\|(\d+)\|([^|]+)\|x=(\d+)\|y=(\d+)\|hw=(\d+)\|hh=(\d+)', line)
        if m:
            patches.append(dict(name=m.group(3), x=int(m.group(4)), y=int(m.group(5)),
                                hw=int(m.group(6)), hh=int(m.group(7))))
    if not patches:
        raise SystemExit('FAIL: no probe patches in %s' % log)
    return patches


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

    patches = read_patches(args.log)
    image = Image.open(args.screenshot).convert('RGB')
    ox, oy = origin(image)
    formula_failures = 0
    for p in patches:
        c = image.getpixel((ox + p['x'], oy + p['y']))
        note = ''
        expect = None
        if p['name'] in ENV:
            expect = texel(*env_uv(ENV[p['name']]))
        elif p['name'] in UV:
            expect = texel(*uv_uv(p['name']))
        if expect:
            # One texel either way: a coordinate on a texel edge is decided by
            # rounding the formula does not model.
            ok = all(abs(a - b) <= 1 or abs(a - b) >= 255 for a, b in zip(c[:2], expect))
            formula_failures += 0 if ok else 1
            note = 'formula texel %s %s' % (expect, 'ok' if ok else 'MISMATCH')
        print('%-22s %-16s %s' % (p['name'], c, note))
    print('formula: %d mismatch(es)' % formula_failures)

    if not args.reference:
        sys.exit(1 if formula_failures else 0)

    ref = Image.open(args.reference).convert('RGB')
    rx, ry = origin(ref)
    failures = 0
    print('\nagainst reference %s (tolerance %d)' % (args.reference, args.tolerance))
    for p in patches:
        worst = 0
        ix, iy = int(p['hw'] * 0.8), int(p['hh'] * 0.8)
        for dy in range(-iy, iy + 1, 2):
            for dx in range(-ix, ix + 1, 2):
                a = image.getpixel((ox + p['x'] + dx, oy + p['y'] + dy))
                b = ref.getpixel((rx + p['x'] + dx, ry + p['y'] + dy))
                worst = max(worst, max(abs(x - y) for x, y in zip(a, b)))
        ok = worst <= args.tolerance
        failures += 0 if ok else 1
        print('%-22s max delta %3d %s' % (p['name'], worst, 'ok' if ok else 'FAIL'))
    print('reference: %d/%d patches within tolerance' % (len(patches) - failures, len(patches)))
    sys.exit(1 if failures or formula_failures else 0)


if __name__ == '__main__':
    main()
