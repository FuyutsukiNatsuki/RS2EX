"""Compare where two renderers put the stencil shadow in the same scene.

Each renderer is captured twice from the same pinned state, shadows on and
shadows off.  A pixel is "in shadow" for a renderer when turning shadows on
made it darker by more than --threshold (luminance, 0-255).  The two shadow
masks are then compared: coverage of each, agreement, and intersection over
union.  Darkness is compared as the mean ratio shadow-on / shadow-off over
the pixels both renderers shadow.

This measures the shadow itself - location, coverage, darkness - and not the
rest of the image, whose remaining differences (text, rasterisation) are
accounted for elsewhere.

Usage:
    python tools/shadow_scene_compare.py ref_on.png ref_off.png cand_on.png cand_off.png [--mask out.png]
"""

import argparse

from PIL import Image

BACKGROUND = (16, 32, 48)


def luminance(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('ref_on')
    parser.add_argument('ref_off')
    parser.add_argument('cand_on')
    parser.add_argument('cand_off')
    parser.add_argument('--threshold', type=float, default=20.0)
    #   The 3D view of a 640 x 480 client inside the 656 x 519 window capture:
    #   below the top bar, left of the icon column.
    parser.add_argument('--box', default='8,64,612,511')
    parser.add_argument('--mask')
    args = parser.parse_args()

    x0, y0, x1, y1 = [int(v) for v in args.box.split(',')]
    images = [Image.open(p).convert('RGB') for p in (args.ref_on, args.ref_off, args.cand_on, args.cand_off)]
    ref_on, ref_off, cand_on, cand_off = [im.load() for im in images]

    total = ref_count = cand_count = both = either = 0
    ratio_ref = ratio_cand = 0.0
    out = Image.new('RGB', (x1 - x0, y1 - y0)) if args.mask else None
    for y in range(y0, y1):
        for x in range(x0, x1):
            total += 1
            lr_on, lr_off = luminance(ref_on[x, y]), luminance(ref_off[x, y])
            lc_on, lc_off = luminance(cand_on[x, y]), luminance(cand_off[x, y])
            r = lr_off - lr_on > args.threshold
            c = lc_off - lc_on > args.threshold
            ref_count += r
            cand_count += c
            both += r and c
            either += r or c
            if r and c:
                ratio_ref += lr_on / max(lr_off, 1.0)
                ratio_cand += lc_on / max(lc_off, 1.0)
            if out:
                #   white: both, red: reference only, green: candidate only
                out.putpixel((x - x0, y - y0), (255, 255, 255) if r and c else
                             (255, 0, 0) if r else (0, 255, 0) if c else (0, 0, 0))
    if out:
        out.save(args.mask)

    iou = both / either if either else 1.0
    agree = 1.0 - (either - both) / total
    print('shadow coverage: reference %.2f%%, candidate %.2f%%' % (100.0 * ref_count / total, 100.0 * cand_count / total))
    print('masks: intersection %d, union %d, IoU %.4f, pixel agreement %.2f%%' % (both, either, iou, 100.0 * agree))
    if both:
        print('darkness (on/off luminance, shared shadow): reference %.3f, candidate %.3f' % (
            ratio_ref / both, ratio_cand / both))


if __name__ == '__main__':
    main()
