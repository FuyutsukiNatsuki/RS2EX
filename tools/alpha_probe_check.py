"""Check the 18 exact interior regions of -dx12alphasmoke screenshots.

The smoke draws a 2x2 palette PNG with alpha bytes 0, 128 and 255 through
the public texture and draw APIs. Blend is disabled so a retained texel has
an exact primary RGB value; a discarded texel leaves the clear colour.
"""

import argparse
from PIL import Image


BACKGROUND = (16, 32, 48)
ROW_COLOURS = ((0, 0, 255), (0, 255, 0), (255, 0, 0))
KEPT = (
    (True, True, True, False, True, False),    # alpha 0
    (True, True, True, False, False, True),    # alpha 128
    (True, True, False, True, False, True),    # alpha 255
)
OFFSETS = ((0, 0), (-16, 0), (16, 0), (0, -16), (0, 16))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot")
    args = parser.parse_args()
    with Image.open(args.screenshot) as source:
        image = source.convert("RGB")

    background_points = [
        (x, y)
        for y in range(image.height)
        for x in range(image.width)
        if image.getpixel((x, y)) == BACKGROUND
    ]
    if not background_points:
        raise SystemExit("FAIL: clear colour not found")
    client_x = min(x for x, _ in background_points)
    client_y = min(y for _, y in background_points)

    failures = []
    for row in range(3):
        for column in range(6):
            expected = ROW_COLOURS[row] if KEPT[row][column] else BACKGROUND
            centre_x = client_x + 60 + column * 100
            centre_y = client_y + 100 + row * 110
            for dx, dy in OFFSETS:
                point = (centre_x + dx, centre_y + dy)
                actual = image.getpixel(point)
                if actual != expected:
                    failures.append(
                        f"row={row} column={column} pixel={point} "
                        f"expected={expected} actual={actual}"
                    )

    for failure in failures[:12]:
        print(f"FAIL: {failure}")
    print(
        f"alpha probe: {90 - len(failures)}/90 exact pixel samples; "
        f"client origin=({client_x},{client_y})"
    )
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
