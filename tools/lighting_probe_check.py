"""Read a -lightingprobe screenshot and check it.

The probe (RS2LightingProbe.cpp) draws 30 patches, one lighting case each.
The patch positions come from the run's own log, so this script never has
to agree with the C++ about where anything is.

Two checks, both useful on their own:

  expected   Each patch is compared with what the Direct3D fixed-function
             lighting equations give for its inputs.  This is how the
             Direct3D 8 run was turned from "an image" into "the contract":
             where the formula and the measured pixels agree, the formula is
             what Direct3D 8 does on this machine.

  --reference
             Each patch is compared with the same patch in another capture,
             typically Direct3D 8 as the reference and Direct3D 12 as the
             candidate.  This is the gate the Direct3D 12 shader has to pass.

Usage:
    python tools/lighting_probe_check.py probe.png probe.log
    python tools/lighting_probe_check.py d3d12.png d3d12.log --reference d3d8.png
"""

import argparse
import math
import re
import sys

from PIL import Image

BACKGROUND = (16, 32, 48)

# The probe's inputs, restated.  If RS2LightingProbe.cpp changes these, this
# table has to change with it; the patch names in the log catch a mismatch.
AMBIENT = 0.25
VERTEX = (0x30 / 255.0, 0x60 / 255.0, 0xc0 / 255.0)
FLAT_TEXTURE = (192 / 255.0, 128 / 255.0, 64 / 255.0)
ALPHA_TEXELS = {'opaque': (1.0, 0.0, 0.0, 1.0), 'half': (0.0, 1.0, 0.0, 128 / 255.0),
                'clear': (0.0, 0.0, 1.0, 0.0)}

M_BASE = dict(d=(0.8, 0.5, 0.2, 1.0), a=(0.4, 0.8, 0.6), s=(0, 0, 0), e=(0, 0, 0), p=0.0)


def material(**changes):
    m = dict(M_BASE)
    m.update(changes)
    return m


MATERIALS = {
    0: material(),
    1: material(e=(0.3, 0.1, 0.5)),
    2: material(d=(1, 1, 1, 1), a=(1, 1, 1), e=(0.5, 0.5, 0.5)),
    3: material(s=(1, 1, 1), p=10.0),
    4: material(s=(1, 1, 1), p=50.0),
    5: material(s=(1, 0, 0), p=10.0),
    6: material(s=(1, 1, 1), p=0.0),
    7: material(d=(0.8, 0.5, 0.2, 0.5)),
    8: material(s=(1, 1, 1), p=1.0),
}

FACING = (0.0, 0.0, -1.0)
HALF = (0.8660254, 0.0, -0.5)
BACK = (0.0, 0.0, 1.0)
PAST_LIGHT = (-0.3, 0.0, -0.9539392)
LIGHT = (0.0, 0.0, 1.0)
BEHIND = (0.9, 0.0, -0.4358899)

# name: (normal, has vertex colour, lighting, specular, dsrc, asrc, n, light,
#        material, texture, blend, ambient)
PATCHES = {
    'unlit-nocolour':         (True, False, False, False, 'm', 'm', FACING, LIGHT, 0, None, False, AMBIENT),
    'lit-facing':             (True, False, True, False, 'm', 'm', FACING, LIGHT, 0, None, False, AMBIENT),
    'lit-half':               (True, False, True, False, 'm', 'm', HALF, LIGHT, 0, None, False, AMBIENT),
    'lit-back':               (True, False, True, False, 'm', 'm', BACK, LIGHT, 0, None, False, AMBIENT),
    'emissive-back':          (True, False, True, False, 'm', 'm', BACK, LIGHT, 1, None, False, AMBIENT),
    'spec-behind-light':      (True, False, True, True, 'm', 'm', PAST_LIGHT, BEHIND, 8, None, False, AMBIENT),
    'src-vv':                 (True, True, True, False, 'v', 'v', FACING, LIGHT, 0, None, False, AMBIENT),
    'src-mm-with-colour':     (True, True, True, False, 'm', 'm', FACING, LIGHT, 0, None, False, AMBIENT),
    'src-vm':                 (True, True, True, False, 'v', 'm', FACING, LIGHT, 0, None, False, AMBIENT),
    'src-vv-nocolour-facing': (True, False, True, False, 'v', 'v', FACING, LIGHT, 0, None, False, AMBIENT),
    'src-vv-nocolour-back':   (True, False, True, False, 'v', 'v', BACK, LIGHT, 0, None, False, AMBIENT),
    'unlit-colour-srcm':      (True, True, False, False, 'm', 'm', FACING, LIGHT, 0, None, False, AMBIENT),
    'spec-off-p10':           (True, False, True, False, 'm', 'm', FACING, LIGHT, 3, None, False, AMBIENT),
    'spec-on-p10':            (True, False, True, True, 'm', 'm', FACING, LIGHT, 3, None, False, AMBIENT),
    'spec-on-p50':            (True, False, True, True, 'm', 'm', FACING, LIGHT, 4, None, False, AMBIENT),
    'spec-on-red':            (True, False, True, True, 'm', 'm', FACING, LIGHT, 5, None, False, AMBIENT),
    'spec-on-p10-textured':   (True, False, True, True, 'm', 'm', FACING, LIGHT, 3, 'flat', False, AMBIENT),
    'spec-on-p0':             (True, False, True, True, 'm', 'm', FACING, LIGHT, 6, None, False, AMBIENT),
    'tex-lit':                (True, False, True, False, 'm', 'm', FACING, LIGHT, 0, 'flat', False, AMBIENT),
    'tex-unlit':              (True, False, False, False, 'm', 'm', FACING, LIGHT, 0, 'flat', False, AMBIENT),
    'tex-vertex-lit':         (True, True, True, False, 'v', 'v', FACING, LIGHT, 0, 'flat', False, AMBIENT),
    'overflow':               (True, False, True, False, 'm', 'm', FACING, LIGHT, 2, None, False, AMBIENT),
    'nonormal-lit':           (False, False, True, False, 'm', 'm', None, LIGHT, 1, None, False, AMBIENT),
    'nonormal-colour-lit':    (False, True, True, False, 'v', 'v', None, LIGHT, 0, None, False, AMBIENT),
    'scaled-facing':          (True, False, True, False, 'm', 'm', FACING, LIGHT, 0, None, False, AMBIENT),
    'diffuse-alpha-blend':    (True, False, True, False, 'm', 'm', FACING, LIGHT, 7, None, True, AMBIENT),
    'alphatest-opaque':       (True, False, True, False, 'm', 'm', FACING, LIGHT, 0, 'opaque', False, AMBIENT),
    'alphatest-cut':          (True, False, True, False, 'm', 'm', FACING, LIGHT, 0, 'clear', False, AMBIENT),
    'texalpha-half-blend':    (True, False, True, False, 'm', 'm', FACING, LIGHT, 0, 'half', True, AMBIENT),
    'ambient-zero-back':      (True, False, True, False, 'm', 'm', BACK, LIGHT, 0, None, False, 0.0),
}


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def normalise(v):
    length = math.sqrt(dot(v, v))
    return tuple(x / length for x in v) if length else v


def clamp(v):
    return max(0.0, min(1.0, v))


def vertex_colour(patch, clip_x, clip_y, cutoff):
    """The fixed-function result at one vertex, before texturing.

    returns (diffuse rgba, specular rgb)
    """
    (has_normal, has_vertex, lighting, specular, dsrc, asrc, n, light,
     mat_index, _texture, _blend, ambient) = patch
    m = MATERIALS[mat_index]
    vertex = VERTEX + (1.0,) if has_vertex else None

    if not lighting:
        colour = vertex if vertex else (1.0, 1.0, 1.0, 1.0)
        return colour, (0.0, 0.0, 0.0)

    # A missing vertex colour falls back to the material - measured, see
    # the src-vv-nocolour patches.
    diffuse = vertex if (dsrc == 'v' and vertex) else m['d']
    amb = vertex[:3] if (asrc == 'v' and vertex) else m['a']
    normal = normalise(n) if (has_normal and n) else (0.0, 0.0, 0.0)
    to_light = tuple(-x for x in normalise(light))
    ndotl = dot(normal, to_light)

    rgb = []
    for i in range(3):
        rgb.append(clamp(m['e'][i] + amb[i] * ambient + diffuse[i] * max(ndotl, 0.0)))

    spec = (0.0, 0.0, 0.0)
    if specular and has_normal and not (cutoff and ndotl <= 0.0):
        view = normalise((-clip_x, -clip_y, -0.5))
        half = normalise(tuple(v + l for v, l in zip(view, to_light)))
        ndoth = max(dot(normal, half), 0.0)
        term = 1.0 if m['p'] == 0.0 else ndoth ** m['p']
        spec = tuple(clamp(m['s'][i] * term) for i in range(3))

    return tuple(rgb) + (diffuse[3],), spec


def expected_centre(name, centre_clip, half_clip, cutoff):
    """The centre pixel: the midpoint of the fan's shared diagonal."""
    patch = PATCHES[name]
    cx, cy = centre_clip
    hw, hh = half_clip
    corners = ((cx - hw, cy + hh), (cx + hw, cy - hh))
    diffuse = [0.0] * 4
    spec = [0.0] * 3
    for x, y in corners:
        d, s = vertex_colour(patch, x, y, cutoff)
        for i in range(4):
            diffuse[i] += d[i] / 2
        for i in range(3):
            spec[i] += s[i] / 2

    texture = patch[9]
    if texture == 'flat':
        texel = FLAT_TEXTURE + (1.0,)
    elif texture:
        texel = ALPHA_TEXELS[texture]
    else:
        texel = (1.0, 1.0, 1.0, 1.0)

    if texture == 'clear':
        return BACKGROUND
    colour = [clamp(texel[i] * diffuse[i] + spec[i]) for i in range(3)]
    alpha = texel[3] * diffuse[3]
    if patch[10]:
        colour = [colour[i] * alpha + BACKGROUND[i] / 255.0 * (1 - alpha) for i in range(3)]
    return tuple(int(round(c * 255)) for c in colour)


def read_patches(log):
    patches = []
    viewport = None
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.search(r'RS2LIGHTPROBE\|begin\|backend=([^|]*)\|viewport=(\d+)x(\d+)', line)
        if m:
            viewport = (int(m.group(2)), int(m.group(3)))
            patches = []
        m = re.search(r'RS2LIGHTPROBE\|patch\|(\d+)\|(\d+)\|([^|]+)\|x=(\d+)\|y=(\d+)\|hw=(\d+)\|hh=(\d+)', line)
        if m:
            patches.append(dict(row=int(m.group(1)), column=int(m.group(2)), name=m.group(3),
                                x=int(m.group(4)), y=int(m.group(5)),
                                hw=int(m.group(6)), hh=int(m.group(7))))
    if not patches or not viewport:
        raise SystemExit('FAIL: no probe patches in %s' % log)
    return viewport, patches


def client_origin(image):
    for y in range(image.height):
        for x in range(image.width):
            if image.getpixel((x, y)) == BACKGROUND:
                return x, y
    raise SystemExit('FAIL: clear colour not found')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('screenshot')
    parser.add_argument('log')
    parser.add_argument('--reference', help='capture to compare patch by patch')
    parser.add_argument('--reference-log', help='log of the reference run (default: same layout)')
    parser.add_argument('--tolerance', type=int, default=2,
                        help='largest channel difference accepted against the reference')
    args = parser.parse_args()

    viewport, patches = read_patches(args.log)
    width, height = viewport
    image = Image.open(args.screenshot).convert('RGB')
    ox, oy = client_origin(image)

    unknown = [p['name'] for p in patches if p['name'] not in PATCHES]
    if unknown:
        raise SystemExit('FAIL: patches this script does not know: %s' % unknown)

    print('client origin (%d,%d), %d patches' % (ox, oy, len(patches)))
    print('%-24s %-15s %-15s %-15s %s' % ('patch', 'measured', 'formula', 'formula+cutoff', 'delta'))
    worst = 0
    for p in patches:
        centre = image.getpixel((ox + p['x'], oy + p['y']))
        clip = (p['x'] / width * 2 - 1, 1 - p['y'] / height * 2)
        half = (p['hw'] * 2.0 / width, p['hh'] * 2.0 / height)
        f = expected_centre(p['name'], clip, half, False)
        fc = expected_centre(p['name'], clip, half, True)
        delta = max(abs(a - b) for a, b in zip(centre, f))
        delta_c = max(abs(a - b) for a, b in zip(centre, fc))
        best = min(delta, delta_c)
        worst = max(worst, best)
        print('%-24s %-15s %-15s %-15s %d%s' % (p['name'], centre, f, fc, best,
                                               '' if f == fc else ' (cutoff matters: %s)' %
                                               ('cutoff' if delta_c < delta else 'no cutoff')))
    print('largest difference from the fixed-function formula: %d' % worst)

    if args.reference:
        ref = Image.open(args.reference).convert('RGB')
        rx, ry = client_origin(ref)
        failures = 0
        print('\nagainst reference %s (tolerance %d)' % (args.reference, args.tolerance))
        for p in patches:
            worst_patch = 0
            # The inner 80% of the patch: its edges are rasterisation, not lighting.
            ix, iy = int(p['hw'] * 0.8), int(p['hh'] * 0.8)
            for dy in range(-iy, iy + 1, 2):
                for dx in range(-ix, ix + 1, 2):
                    a = image.getpixel((ox + p['x'] + dx, oy + p['y'] + dy))
                    b = ref.getpixel((rx + p['x'] + dx, ry + p['y'] + dy))
                    worst_patch = max(worst_patch, max(abs(x - y) for x, y in zip(a, b)))
            ok = worst_patch <= args.tolerance
            failures += 0 if ok else 1
            print('%-24s max delta %3d %s' % (p['name'], worst_patch, 'ok' if ok else 'FAIL'))
        print('reference: %d/%d patches within tolerance' % (len(patches) - failures, len(patches)))
        if failures:
            sys.exit(1)


if __name__ == '__main__':
    main()
