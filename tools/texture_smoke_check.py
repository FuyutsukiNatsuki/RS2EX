"""Check deterministic interior pixels from -dx12texturesmoke screenshots."""

import argparse

from PIL import Image


BACKGROUND = (16, 32, 48)
EXPECTED = (
    ((255, 0, 0), (0, 255, 0), (255, 0, 0), (128, 128, 0)),
    ((0, 0, 255), BACKGROUND, (0, 255, 0), (32, 192, 224)),
    ((224, 32, 192), BACKGROUND, (0, 0, 0), (48, 48, 48)),
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
        for column in range(4):
            expected = EXPECTED[row][column]
            tolerance = 1 if (row, column) == (0, 3) else 0
            centre_x = client_x + 95 + column * 150
            centre_y = client_y + 90 + row * 140
            for dx, dy in OFFSETS:
                point = (centre_x + dx, centre_y + dy)
                actual = image.getpixel(point)
                if any(abs(a - e) > tolerance for a, e in zip(actual, expected)):
                    failures.append(
                        f"row={row} column={column} pixel={point} "
                        f"expected={expected} actual={actual}"
                    )

    for failure in failures[:12]:
        print(f"FAIL: {failure}")
    print(
        f"texture smoke: {60 - len(failures)}/60 pixel samples; "
        f"client origin=({client_x},{client_y})"
    )
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
