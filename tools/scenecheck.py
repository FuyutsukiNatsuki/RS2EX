"""Does this screenshot still have a scene in it?

RS2EX v0.0.9 shipped a candidate where the user interface drew normally and
every mesh was missing, and every automated check in the release passed: the
build was clean, the dependency scan read zero, the draw calls were provably
equivalent to the baseline, and mesh import was byte-identical.  None of them
looked at the picture.

Pixel comparison would have caught it, but the test scene is not
deterministic - the sky changes colour with the in-game clock and the train
and camera move - so the same binary differs from itself in most pixels.

This measures structure instead, which survives a scene that keeps moving:

  edge%    viewport pixels whose 3x3 gradient exceeds a threshold.  Geometry
           has silhouettes, rails, girders, platform edges.  A sky gradient
           has none.
  colours  distinct colours after quantising to 5 bits per channel.

Against the actual defect, working builds measured 1.41-3.61% and 373-410
colours; the broken ones measured 0.17-0.18% and 55-69.  Eight times at the
narrowest, so this is a floor and not an equality test.  It answers "is there
still a scene", not "is the scene right".  For the second question run the
program with -fixture, where frames are reproducible to the pixel, and use
--compare.

Usage:
    python tools/scenecheck.py shot.png [shot.png ...]
    python tools/scenecheck.py --min-edge 1.0 --min-colours 200 shot.png
    python tools/scenecheck.py --compare before.png after.png

Exit status is 0 when every image passes the floors given, 1 otherwise, so
this can gate a build.  With no floors it only reports.

Requires Pillow and numpy.
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image

#   Window-relative viewport: inside the frame, below the information bar and
#   above the wind-speed strip.  Matches what tools/rs2shot.ps1 captures.  The
#   bars are excluded deliberately - they keep drawing when the scene does not,
#   which is exactly the case this has to notice.
DEFAULT_VIEWPORT = (14, 58, 610, 484)

#   Chosen so ordinary shading gradients do not register as edges but object
#   silhouettes do.  The measured gap either side of it is large enough that
#   the exact value is not delicate.
EDGE_THRESHOLD = 24.0


def load(path, viewport):
    im = Image.open(path).convert('RGB')

    left, top, right, bottom = viewport
    if right > im.width or bottom > im.height:
        raise SystemExit(
            '%s is %dx%d, too small for viewport %s'
            % (path, im.width, im.height, viewport))
    return im.crop(viewport)


def measure(im):
    """Return (edge fraction, distinct quantised colours)."""
    a = np.asarray(im).astype(np.float64)
    grey = a.mean(axis=2)

    #   Trim both differences to the same shape so they can be combined.
    dy = np.abs(np.diff(grey, axis=0))[:, :-1]
    dx = np.abs(np.diff(grey, axis=1))[:-1, :]
    edge = float((np.hypot(dx, dy) > EDGE_THRESHOLD).mean())

    q = (np.asarray(im) >> 3).astype(np.uint32)
    packed = (q[:, :, 0] << 10) | (q[:, :, 1] << 5) | q[:, :, 2]
    return edge, int(np.unique(packed).size)


def compare(a_path, b_path, viewport, tolerance_pct):
    """Compare two frames captured under -fixture.

    Two fixture captures of the same build are byte-identical, so anything
    reported here is a real difference.  The bounding box is printed because
    knowing where a change is usually identifies it faster than knowing how
    large it is.
    """
    a = np.asarray(load(a_path, viewport)).astype(np.int16)
    b = np.asarray(load(b_path, viewport)).astype(np.int16)

    differing = np.any(a != b, axis=2)
    count = int(differing.sum())
    total = int(differing.size)

    print('%s vs %s' % (os.path.basename(a_path), os.path.basename(b_path)))

    if not count:
        print('  identical')
        return 0

    ys, xs = np.nonzero(differing)
    pct = 100.0 * count / total

    print('  differing pixels : %d / %d (%.4f%%)' % (count, total, pct))
    print('  largest channel delta : %d' % int(np.abs(a - b).max()))
    print('  bounding box : x %d..%d, y %d..%d (viewport-relative)'
          % (xs.min(), xs.max(), ys.min(), ys.max()))

    if pct <= tolerance_pct:
        print('  within the %.4f%% tolerance allowed' % tolerance_pct)
        return 0

    print('  DIFFERENT - check the bounding box against the known residual '
          'before blaming the change')
    return 1


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('images', nargs='+', help='PNG screenshots to measure')
    p.add_argument('--viewport', metavar='L,T,R,B',
                   help='crop box, default %s' % (DEFAULT_VIEWPORT,))
    p.add_argument('--min-edge', type=float, metavar='PCT',
                   help='fail if edge%% falls below this')
    p.add_argument('--min-colours', type=int, metavar='N',
                   help='fail if the colour count falls below this')
    p.add_argument('--compare', action='store_true',
                   help='compare two images exactly instead of measuring')
    p.add_argument('--tolerance', type=float, default=0.0, metavar='PCT',
                   help='with --compare, allow this %% of pixels to differ '
                        '(default 0)')
    args = p.parse_args(argv)

    viewport = DEFAULT_VIEWPORT
    if args.viewport:
        parts = args.viewport.split(',')
        if len(parts) != 4:
            raise SystemExit('--viewport wants four comma-separated numbers')
        viewport = tuple(int(v) for v in parts)

    if args.compare:
        if len(args.images) != 2:
            raise SystemExit('--compare wants exactly two images')
        return compare(args.images[0], args.images[1], viewport, args.tolerance)

    failed = False
    print('%-28s %9s %9s' % ('image', 'edge%', 'colours'))

    for path in args.images:
        if not os.path.exists(path):
            print('%-28s %9s %9s' % (os.path.basename(path), 'missing', '-'))
            failed = True
            continue

        edge, colours = measure(load(path, viewport))
        edge_pct = edge * 100.0

        note = ''
        if args.min_edge is not None and edge_pct < args.min_edge:
            note += ' EDGE BELOW %.2f' % args.min_edge
            failed = True
        if args.min_colours is not None and colours < args.min_colours:
            note += ' COLOURS BELOW %d' % args.min_colours
            failed = True

        print('%-28s %9.3f %9d%s'
              % (os.path.basename(path), edge_pct, colours, note))

    if failed:
        print('')
        print('FAIL - a screenshot has lost its scene, or is missing')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
