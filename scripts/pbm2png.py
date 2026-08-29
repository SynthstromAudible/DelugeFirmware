#!/usr/bin/env python3
"""Render P1 (plain-text) PBM snapshots as PNG, for PR viewing (spec G §3, §7).

  python3 scripts/pbm2png.py in.pbm -o out.png [--scale 4]
  python3 scripts/pbm2png.py golden.pbm got.pbm -o diff.png [--scale 4]

One input renders as a grayscale PNG (1-bit PBM -> 8-bit, black-on-white, per the PBM
convention that a pixel value of 1 is black and 0 is white). Two inputs render side by side
as an RGB PNG, separated by a 4px pure-red gutter, so a golden/got pair can be eyeballed in a
PR without a diff tool that understands PBM. `--scale` repeats each pixel into an NxN block
(nearest-neighbour) before the gutter is added, since these images are small and 1:1 is hard
to read.

Only a P1 (ASCII) PBM is accepted; P4 (binary) is rejected with an explanation, since nothing
in this codebase writes P4 snapshots and silently accepting it would be a maintenance trap. No
non-stdlib dependencies: only `zlib` (DEFLATE + CRC32) and `struct` (chunk framing).

Exit codes: 0 success, 1 a file was not a valid/supported PBM, 2 a usage error.
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path


def parse_p1_pbm(data: bytes) -> tuple[int, int, list[int]]:
    """Parse a P1 PBM into `(width, height, pixels)`; `pixels` is a flat row-major 0/1 list."""
    text = "\n".join(
        line.split("#", 1)[0]
        for line in data.decode("ascii", errors="replace").splitlines()
    )
    tokens = text.split()
    if not tokens:
        raise ValueError("empty file")
    if tokens[0] == "P4":
        raise ValueError("P4 (binary) PBM is not supported -- only P1 (plain-text) is")
    if tokens[0] != "P1":
        raise ValueError(f"not a P1 PBM (magic {tokens[0]!r})")
    if len(tokens) < 3:
        raise ValueError("truncated PBM header")
    try:
        width, height = int(tokens[1]), int(tokens[2])
    except ValueError as exc:
        raise ValueError(f"bad PBM dimensions {tokens[1]!r} {tokens[2]!r}") from exc
    bits = tokens[3:]
    if len(bits) != width * height:
        raise ValueError(f"expected {width * height} pixel values, found {len(bits)}")
    if any(b not in ("0", "1") for b in bits):
        raise ValueError("non-binary pixel value in raster")
    return width, height, [int(b) for b in bits]


def to_grayscale(width: int, height: int, pixels: list[int]) -> list[int]:
    """PBM 1=black/0=white -> 8-bit grayscale (0=black, 255=white), row-major."""
    return [0 if bit else 255 for bit in pixels]


def scale_grayscale(
    width: int, height: int, gray: list[int], scale: int
) -> tuple[int, int, list[int]]:
    """Nearest-neighbour scale a grayscale image by an integer factor."""
    if scale == 1:
        return width, height, gray
    out_w, out_h = width * scale, height * scale
    out = [0] * (out_w * out_h)
    for y in range(height):
        for x in range(width):
            v = gray[y * width + x]
            for dy in range(scale):
                row = (y * scale + dy) * out_w
                for dx in range(scale):
                    out[row + x * scale + dx] = v
    return out_w, out_h, out


GUTTER_PX = 4
GUTTER_RGB = (255, 0, 0)


def side_by_side_rgb(
    left: tuple[int, int, list[int]],
    right: tuple[int, int, list[int]],
) -> tuple[int, int, list[int]]:
    """Compose two grayscale images side by side, RGB, with a pure-red gutter between them.

    Shorter image is top-aligned and padded with white so mismatched heights (e.g. a display
    size change) still render instead of crashing.
    """
    lw, lh, lgray = left
    rw, rh, rgray = right
    height = max(lh, rh)
    width = lw + GUTTER_PX + rw
    out = [255] * (width * height * 3)  # white background, RGB

    def blit(gray: list[int], gw: int, gh: int, x_off: int) -> None:
        for y in range(gh):
            for x in range(gw):
                v = gray[y * gw + x]
                idx = (y * width + x_off + x) * 3
                out[idx] = out[idx + 1] = out[idx + 2] = v

    blit(lgray, lw, lh, 0)
    for y in range(height):
        for gx in range(GUTTER_PX):
            idx = (y * width + lw + gx) * 3
            out[idx], out[idx + 1], out[idx + 2] = GUTTER_RGB
    blit(rgray, rw, rh, lw + GUTTER_PX)
    return width, height, out


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data))
    )


def write_png(
    path: Path, width: int, height: int, pixels: list[int], channels: int
) -> None:
    """Write a PNG. `channels` is 1 (grayscale, color type 0) or 3 (RGB, color type 2)."""
    color_type = 0 if channels == 1 else 2
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    raw = bytearray()
    stride = width * channels
    for y in range(height):
        raw.append(0)  # filter type 0 (none) per scanline
        raw.extend(pixels[y * stride : (y + 1) * stride])
    idat = zlib.compress(bytes(raw), level=9)
    with path.open("wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(png_chunk(b"IHDR", ihdr))
        f.write(png_chunk(b"IDAT", idat))
        f.write(png_chunk(b"IEND", b""))


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("pbm", type=Path, help="input P1 PBM")
    ap.add_argument(
        "other_pbm",
        type=Path,
        nargs="?",
        help="optional second PBM, rendered side by side",
    )
    ap.add_argument("-o", "--out", type=Path, required=True, help="output PNG path")
    ap.add_argument(
        "--scale",
        type=int,
        default=1,
        help="nearest-neighbour integer upscale (default 1)",
    )
    args = ap.parse_args()

    if args.scale < 1:
        print(f"error: --scale must be >= 1, got {args.scale}", file=sys.stderr)
        return 2

    for p in (args.pbm, args.other_pbm):
        if p is not None and not p.is_file():
            print(f"error: not a file: {p}", file=sys.stderr)
            return 2

    try:
        width, height, pixels = parse_p1_pbm(args.pbm.read_bytes())
        gray = to_grayscale(width, height, pixels)
        left = scale_grayscale(width, height, gray, args.scale)

        if args.other_pbm is not None:
            ow, oh, opixels = parse_p1_pbm(args.other_pbm.read_bytes())
            ogray = to_grayscale(ow, oh, opixels)
            right = scale_grayscale(ow, oh, ogray, args.scale)
            out_w, out_h, out_pixels = side_by_side_rgb(left, right)
            write_png(args.out, out_w, out_h, out_pixels, channels=3)
        else:
            out_w, out_h, out_pixels = left
            write_png(args.out, out_w, out_h, out_pixels, channels=1)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
