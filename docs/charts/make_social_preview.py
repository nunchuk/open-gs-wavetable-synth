#!/usr/bin/env python3
"""Write docs/images/open-gs-about.png, README.md's header and the repository's social preview.

A Windows 9x "About" dialog for Open GS on the teal desktop, drawn pixel by pixel at 640 x 320
with unsmoothed Tahoma (the classic Windows UI face) and doubled with nearest-neighbour scaling.
Everything in it is drawn here: no Microsoft or Roland artwork.

The image is 1280 x 640, the size GitHub asks for a social preview; upload it in the repository's
Settings > General > Social preview after a change. Its numbers are README.md's: change both.

Usage (Pillow, pinned in docs/requirements.txt, in a virtualenv):
    python3 make_social_preview.py [--fonts DIR] [OUTPUT]   (DIR holds Tahoma.ttf, Tahoma Bold.ttf)
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path("/System/Library/Fonts/Supplemental")  # Tahoma ships with macOS; --fonts elsewhere
DESKTOP, FACE, WHITE, LIGHT, SHADOW, BLACK = "#008080", "#c0c0c0", "#ffffff", "#dfdfdf", "#808080", "#000000"
NAVY, BLUE, SCREEN, WAVE = (0, 0, 128), (16, 132, 208), "#003a3a", "#5ff2c8"


def font(bold: bool, size: int) -> ImageFont.FreeTypeFont:
    """Return Tahoma (bold or regular) at a pixel size."""
    return ImageFont.truetype(str(FONTS / ("Tahoma Bold.ttf" if bold else "Tahoma.ttf")), size)


def bevel(d: ImageDraw.ImageDraw, x0: int, y0: int, x1: int, y1: int, raised: bool = True) -> None:
    """Draw the classic two-pixel 3D border inside the box (x0, y0)-(x1, y1), inclusive."""
    outer_tl, outer_br, inner_tl, inner_br = ((LIGHT, BLACK, WHITE, SHADOW) if raised
                                              else (SHADOW, WHITE, BLACK, LIGHT))
    d.line([(x0, y1), (x0, y0), (x1, y0)], fill=outer_tl)
    d.line([(x0, y1), (x1, y1), (x1, y0)], fill=outer_br)
    d.line([(x0 + 1, y1 - 1), (x0 + 1, y0 + 1), (x1 - 1, y0 + 1)], fill=inner_tl)
    d.line([(x0 + 1, y1 - 1), (x1 - 1, y1 - 1), (x1 - 1, y0 + 1)], fill=inner_br)


def etched(d: ImageDraw.ImageDraw, x0: int, y0: int, x1: int, y1: int) -> None:
    """Draw an etched (grooved) rectangle, as a group box's frame."""
    d.rectangle([x0, y0, x1 - 1, y1 - 1], outline=SHADOW)
    d.rectangle([x0 + 1, y0 + 1, x1, y1], outline=WHITE)
    d.line([(x0 + 1, y1 - 1), (x0 + 1, y0 + 1), (x1 - 1, y0 + 1)], fill=WHITE)
    d.line([(x0, y0), (x1 - 1, y0)], fill=SHADOW)
    d.line([(x0, y0), (x0, y1 - 1)], fill=SHADOW)


def group(d: ImageDraw.ImageDraw, x0: int, y0: int, x1: int, y1: int, label: str) -> None:
    """Draw a group box with its label cut into the top edge."""
    etched(d, x0, y0, x1, y1)
    f = font(False, 11)
    w = int(d.textlength(label, font=f))
    d.rectangle([x0 + 7, y0 - 6, x0 + 9 + w, y0 + 6], fill=FACE)
    d.text((x0 + 8, y0 - 7), label, font=f, fill=BLACK)


def icon(d: ImageDraw.ImageDraw, x: int, y: int, scale: int = 1) -> None:
    """A small synthesizer: a display with a wave over a keyboard (32 x 32 at scale 1)."""
    def px(cx: int, cy: int, w: int, h: int, c: str) -> None:
        d.rectangle([x + cx * scale, y + cy * scale, x + (cx + w) * scale - 1, y + (cy + h) * scale - 1], fill=c)
    px(0, 2, 32, 28, BLACK)               # case outline
    px(1, 3, 30, 26, SHADOW)
    px(1, 3, 30, 1, WHITE)
    px(4, 5, 24, 9, SCREEN)               # display
    wave = [3, 2, 1, 1, 2, 3, 5, 6, 7, 7, 6, 5, 3, 2, 1, 1, 2, 3, 5, 6, 7, 7, 6, 5]
    for i, h in enumerate(wave):
        px(4 + i, 5 + h, 1, 1, WAVE)
    px(3, 17, 26, 11, BLACK)              # keyboard
    for k in range(6):
        px(4 + k * 4, 18, 3, 9, WHITE)
    for k in (0, 1, 3, 4):
        px(6 + k * 4, 18, 2, 5, BLACK)


def close_button(d: ImageDraw.ImageDraw, x: int, y: int) -> None:
    """The title bar's 16 x 14 close button."""
    d.rectangle([x, y, x + 15, y + 13], fill=FACE)
    bevel(d, x, y, x + 15, y + 13)
    for i in range(6):  # a 2-pixel-wide X, 6 rows
        d.point([(x + 4 + i, y + 3 + i), (x + 5 + i, y + 3 + i), (x + 9 - i, y + 3 + i), (x + 10 - i, y + 3 + i)],
                fill=BLACK)


def button(d: ImageDraw.ImageDraw, x0: int, y0: int, x1: int, y1: int, label: str) -> None:
    """A default push button: the black frame, the bevel, the focus rectangle and the label."""
    d.rectangle([x0, y0, x1, y1], fill=FACE, outline=BLACK)
    bevel(d, x0 + 1, y0 + 1, x1 - 1, y1 - 1)
    for xx in range(x0 + 4, x1 - 3, 2):
        d.point([(xx, y0 + 4), (xx, y1 - 4)], fill=BLACK)
    for yy in range(y0 + 4, y1 - 3, 2):
        d.point([(x0 + 4, yy), (x1 - 4, yy)], fill=BLACK)
    f = font(False, 11)
    w = int(d.textlength(label, font=f))
    d.text(((x0 + x1 - w) // 2, (y0 + y1) // 2 - 7), label, font=f, fill=BLACK)


def draw() -> Image.Image:
    """Return the preview at 640 x 320 (before the doubling)."""
    img = Image.new("RGB", (640, 320), DESKTOP)
    d = ImageDraw.Draw(img)
    d.fontmode = "1"  # no anti-aliasing: whole pixels, as Windows 9x drew text
    x0, y0, x1, y1 = 64, 26, 575, 293
    d.rectangle([x0, y0, x1, y1], fill=FACE)
    bevel(d, x0, y0, x1, y1)
    # the title bar: the Windows 98 gradient, the icon, the title and the close button
    for i in range(x1 - x0 - 5):
        t = i / (x1 - x0 - 6)
        c = tuple(round(a + (b - a) * t) for a, b in zip(NAVY, BLUE))
        d.line([(x0 + 3 + i, y0 + 3), (x0 + 3 + i, y0 + 20)], fill=c)
    tile = Image.new("RGBA", (32, 32), (0, 0, 0, 0))  # transparent, over the gradient
    icon(ImageDraw.Draw(tile), 0, 0)
    small = tile.resize((16, 16), Image.NEAREST)
    img.paste(small, (x0 + 5, y0 + 4), small)
    d.text((x0 + 25, y0 + 4), "About Open GS", font=font(True, 11), fill=WHITE)
    close_button(d, x1 - 21, y0 + 5)
    # the icon and the name
    icon(d, x0 + 22, y0 + 38)
    d.text((x0 + 72, y0 + 32), "Open GS", font=font(True, 24), fill=BLACK)
    body = font(False, 11)
    d.text((x0 + 73, y0 + 64), "The Microsoft GS Wavetable Synth of Windows,", font=body, fill=BLACK)
    d.text((x0 + 73, y0 + 78), "reimplemented in C++20, bit-exact. MIT licensed.", font=body, fill=BLACK)
    # what was verified
    gx0, gx1 = x0 + 22, x1 - 22
    group(d, gx0, y0 + 108, gx1, y0 + 170, "Verified against the Windows synth")
    rows = (("Recordings replayed", "52"), ("Frames compared", "64 036 275"), ("Differing samples", "0"))
    for i, (label, value) in enumerate(rows):
        yy = y0 + 118 + i * 15
        d.text((gx0 + 14, yy), label + ":", font=body, fill=BLACK)
        v = font(True, 11) if value == "0" else body
        d.text((gx1 - 14 - int(d.textlength(value, font=v)), yy), value, font=v, fill=BLACK)
    # where it runs
    group(d, gx0, y0 + 184, gx1, y0 + 214, "Renders MIDI with gm.dls on")
    platforms = "macOS      iOS      Linux      Windows      WebAssembly"
    d.text(((gx0 + gx1 - int(d.textlength(platforms, font=body))) // 2, y0 + 194), platforms, font=body, fill=BLACK)
    # the repository and OK
    d.text((gx0, y0 + 236), "github.com/nunchuk/open-gs-wavetable-synth", font=body, fill=SHADOW)
    d.text((gx0 + 1, y0 + 237), "github.com/nunchuk/open-gs-wavetable-synth", font=body, fill=WHITE)
    d.text((gx0, y0 + 236), "github.com/nunchuk/open-gs-wavetable-synth", font=body, fill=BLACK)
    button(d, gx1 - 75, y0 + 229, gx1, y0 + 251, "OK")
    return img


def main() -> None:
    """Command-line entry point."""
    global FONTS
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    default = Path(__file__).resolve().parent.parent / "images" / "open-gs-about.png"
    parser.add_argument("output", type=Path, nargs="?", default=default)
    parser.add_argument("--fonts", type=Path, default=FONTS, help="the directory with Tahoma.ttf")
    args = parser.parse_args()
    FONTS = args.fonts
    draw().resize((1280, 640), Image.NEAREST).save(args.output, optimize=True)
    print(f"{args.output}: 1280 x 640")


if __name__ == "__main__":
    main()
