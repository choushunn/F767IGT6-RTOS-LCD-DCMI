#!/usr/bin/env python3
"""Render an RGB565 raw framebuffer and print diagnostic statistics."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("width", type=int)
    parser.add_argument("height", type=int)
    parser.add_argument("--pitch", type=int)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    pitch = args.pitch or args.width
    raw = np.fromfile(args.input, dtype="<u2")
    required = pitch * args.height
    if raw.size < required:
        raise SystemExit(
            f"{args.input} is too small: {raw.size} words, need {required}"
        )

    image_words = raw[:required].reshape(args.height, pitch)[:, :args.width]
    low = (image_words & 0xFF).astype(np.uint8)
    high = (image_words >> 8).astype(np.uint8)

    rgb = np.dstack(
        (
            ((image_words >> 11) & 0x1F).astype(np.uint16) * 255 // 31,
            ((image_words >> 5) & 0x3F).astype(np.uint16) * 255 // 63,
            (image_words & 0x1F).astype(np.uint16) * 255 // 31,
        )
    ).astype(np.uint8)

    output = args.output or args.input.with_suffix(".png")
    output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgb).save(output)
    unique_words = len(np.unique(image_words))
    unique_colors = len(np.unique(rgb.reshape(-1, 3), axis=0))
    low_eq_high = float((low == high).mean() * 100.0)
    row_unique = np.array([len(np.unique(row)) for row in image_words])

    print(f"image={output}")
    print(f"shape={args.height}x{args.width} pitch={pitch}")
    print(f"unique_words={unique_words} unique_colors={unique_colors}")
    print(f"low_eq_high={low_eq_high:.2f}%")
    print(
        "row_unique: "
        f"mean={row_unique.mean():.1f} min={row_unique.min()} max={row_unique.max()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
