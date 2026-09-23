"""Measure the ground-texture region against the accepted v0.1.1 D3D12 image.

Both inputs are 656x519 windowed captures of the 640x480 fixture. This is a
Stage 0 texture-detail check, not a whole-scene parity comparison: the old
renderer intentionally rendered this region plain white.
"""

import argparse

from PIL import Image


REGION = (24, 350, 160, 480)
WHITE = (255, 255, 255)


def pixels(image):
    x0, y0, x1, y1 = REGION
    return [image.getpixel((x, y)) for y in range(y0, y1) for x in range(x0, x1)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("v011_textureless")
    parser.add_argument("current")
    args = parser.parse_args()
    with Image.open(args.v011_textureless) as source:
        baseline = source.convert("RGB")
    with Image.open(args.current) as source:
        current = source.convert("RGB")
    if baseline.size != (656, 519) or current.size != (656, 519):
        raise SystemExit("FAIL: expected two 656x519 windowed fixture screenshots")

    old = pixels(baseline)
    new = pixels(current)
    changed = sum(a != b for a, b in zip(old, new))
    nonwhite = sum(pixel != WHITE for pixel in new)
    old_colours = len(set(old))
    new_colours = len(set(new))
    print(
        f"ground region {REGION}: changed={changed}/{len(new)}, "
        f"nonwhite={nonwhite}/{len(new)}, "
        f"colours={old_colours}->{new_colours}"
    )
    if old_colours != 1 or old[0] != WHITE:
        raise SystemExit("FAIL: v0.1.1 ground baseline is not plain white")
    if changed < len(new) * 0.9 or nonwhite < len(new) * 0.9 or new_colours < 100:
        raise SystemExit("FAIL: recognizable ground texture detail was not measured")
    print("texture scene: pass")


if __name__ == "__main__":
    main()
