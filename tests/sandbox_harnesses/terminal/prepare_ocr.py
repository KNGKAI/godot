"""Rectify native framebuffer crops using their recorded camera projections."""

import json
import sys
from pathlib import Path

from PIL import Image, ImageOps


def rectify(source, sample):
    width, height = (round(span * 2) for span in sample["label_size"])
    origin_x, origin_y = sample["origin"]
    corners = [
        ((x - origin_x) * depth, (y - origin_y) * depth, depth)
        for x, y, depth in sample["corners"]
    ]
    # Homogeneous screen coordinates vary linearly across the planar label.
    top_left, top_right, _, bottom_left = corners
    horizontal = [(b - a) / width for a, b in zip(top_left, top_right)]
    vertical = [(b - a) / height for a, b in zip(top_left, bottom_left)]
    coefficients = (
        horizontal[0], vertical[0], top_left[0],
        horizontal[1], vertical[1], top_left[1],
        horizontal[2], vertical[2],
    )
    coefficients = [value / top_left[2] for value in coefficients]
    rectified = source.convert("L").transform(
        (width, height), Image.Transform.PERSPECTIVE, coefficients, Image.Resampling.BICUBIC
    )
    # Dark ink on white paper is the input format for the English OCR pass.
    return ImageOps.expand(ImageOps.invert(ImageOps.autocontrast(rectified)), 24, fill="white")


def main():
    output = Path(sys.argv[1])
    report = json.loads((output / "room.json").read_text(encoding="utf-8"))
    destination = output / "ocr"
    destination.mkdir(exist_ok=True)
    for sample in report["observations"]:
        with Image.open(output / sample["file"]) as image:
            rectify(image, sample).save(destination / sample["file"])
    Image.new("L", (300, 100), "white").save(destination / "blank.png")


if __name__ == "__main__":
    main()
