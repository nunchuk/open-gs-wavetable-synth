#!/usr/bin/env python3
"""Write the README's charts as SVG, one file per chart and theme (light and dark).

converters-{light,dark}.svg   from converter_response.csv (converter_response.cpp makes it)
bitexact-{light,dark}.svg     from a replay of a recording of the Windows synth: an excerpt
                              (ms,windows,library) and the largest difference per block
                              (seconds,max_abs_difference). The recordings are not distributed,
                              so neither is that data; the committed SVG is its only record.

Usage:
    python3 make_charts.py [--excerpt EXCERPT.csv --difference DIFFERENCE.csv]

The colours, marks and chrome follow a validated categorical palette (slot 1 blue, slot 2
orange, checked for colour-vision deficiency in both themes).
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import NamedTuple

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "images"
FONT = "system-ui, -apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"


class Theme(NamedTuple):
    """The chart chrome and series colours of one theme."""

    name: str
    surface: str
    primary: str
    secondary: str
    muted: str
    grid: str
    axis: str
    series1: str
    series2: str


LIGHT = Theme("light", "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7", "#2a78d6", "#eb6834")
DARK = Theme("dark", "#1a1a19", "#ffffff", "#c3c2b7", "#898781", "#2c2c2a", "#383835", "#3987e5", "#d95926")


def esc(text: str) -> str:
    """Escape text for SVG."""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Panel(NamedTuple):
    """A plot area and the data range it maps."""

    x: float
    y: float
    w: float
    h: float
    x0: float
    x1: float
    y0: float
    y1: float

    def px(self, v: float) -> float:
        """Map a data x to pixels."""
        return self.x + (v - self.x0) / (self.x1 - self.x0) * self.w

    def py(self, v: float) -> float:
        """Map a data y to pixels."""
        return self.y + (self.y1 - v) / (self.y1 - self.y0) * self.h


class Svg:
    """A minimal SVG builder."""

    def __init__(self, width: int, height: int, theme: Theme, title: str, desc: str) -> None:
        self.t = theme
        self.clips = 0
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" role="img" font-family="{FONT}">',
            f"<title>{esc(title)}</title><desc>{esc(desc)}</desc>",
            f'<rect width="{width}" height="{height}" rx="8" fill="{theme.surface}"/>',
        ]

    def text(self, x: float, y: float, s: str, size: int = 12, ink: str = "", anchor: str = "start",
             weight: int = 400) -> None:
        """Add a text label in a text ink (never a series colour)."""
        self.parts.append(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{ink or self.t.secondary}" '
                          f'text-anchor="{anchor}" font-weight="{weight}">{esc(s)}</text>')

    def line(self, x1: float, y1: float, x2: float, y2: float, ink: str, width: float = 1) -> None:
        """Add a straight hairline or rule."""
        self.parts.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{ink}" '
                          f'stroke-width="{width}"/>')

    def series(self, p: Panel, points: list[tuple[float, float]], ink: str, width: float = 2) -> None:
        """Add a 2 px data line clipped to its panel."""
        self.clips += 1
        cid = f"clip{self.clips}"
        d = " ".join(f"{'M' if i == 0 else 'L'}{p.px(x):.2f},{p.py(y):.2f}" for i, (x, y) in enumerate(points))
        self.parts.append(f'<clipPath id="{cid}"><rect x="{p.x}" y="{p.y - 2}" width="{p.w}" height="{p.h + 4}"/>'
                          f'</clipPath>')
        self.parts.append(f'<path d="{d}" fill="none" stroke="{ink}" stroke-width="{width}" '
                          f'stroke-linejoin="round" stroke-linecap="round" clip-path="url(#{cid})"/>')

    def dot(self, x: float, y: float, ink: str) -> None:
        """Add an 8 px marker with a 2 px surface ring."""
        self.parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{ink}" stroke="{self.t.surface}" '
                          f'stroke-width="2"/>')

    def grid(self, p: Panel, yticks: list[float], ylabel, xticks: list[float], xlabel) -> None:
        """Add hairline gridlines at the y ticks, the baseline and the tick labels."""
        for v in yticks:
            y = p.py(v)
            self.line(p.x, y, p.x + p.w, y, self.t.grid)
            self.text(p.x - 8, y + 4, ylabel(v), 11, self.t.muted, "end")
        self.line(p.x, p.y + p.h, p.x + p.w, p.y + p.h, self.t.axis)
        for v in xticks:
            self.text(p.px(v), p.y + p.h + 16, xlabel(v), 11, self.t.muted, "middle")

    def key(self, x: float, y: float, ink: str, label: str) -> float:
        """Add a legend entry (a short line in the series colour, text in ink); return its end x."""
        self.line(x, y - 4, x + 18, y - 4, ink, 2)
        self.text(x + 24, y, label, 12, self.t.primary)
        return x + 24 + 7.0 * len(label) + 20

    def write(self, path: Path) -> None:
        """Write the file."""
        path.write_text("\n".join(self.parts + ["</svg>"]) + "\n")


def minus(s: str) -> str:
    """Use a true minus sign in labels."""
    return s.replace("-", "−")


def converters(theme: Theme) -> None:
    """The two converters' tone and image levels against frequency."""
    rows = list(csv.DictReader(open(HERE / "converter_response.csv")))
    hz = [float(r["hz"]) / 1000 for r in rows]
    tg = [float(r["transparent_tone_db"]) for r in rows]
    ti = [float(r["transparent_image_db"]) for r in rows]
    wg = [float(r["windows_tone_db"]) for r in rows]
    wi = [float(r["windows_image_db"]) for r in rows]
    at10 = next(i for i, r in enumerate(rows) if r["hz"] == "10000")
    width, height = 760, 560
    s = Svg(width, height, theme, "Two ways from the synth's 22 050 Hz to 48 kHz",
            "A tone goes in at each frequency up to 11 kHz. Transparent: the tone comes out within 0.0003 dB up "
            f"to 10.5 kHz and no image comes within 103 dB. Windows 10's converter: {wg[at10]:.1f} dB at 10 kHz, "
            f"{wg[-1]:.1f} dB at 11 kHz, images up to {max(wi):.0f} dB.")
    s.text(32, 36, "Two ways from the synth’s 22 050 Hz to 48 kHz", 17, theme.primary, weight=600)
    s.text(32, 58, "A tone at each frequency goes in. Top: how loud it comes out. Bottom: the loudest image it "
                   "adds (dB re the tone in).", 12, theme.secondary)
    x = s.key(32, 86, theme.series1, "transparent (the default)")
    s.key(x, 86, theme.series2, "windows (Windows 10’s audio engine)")
    xt = [0, 2, 4, 6, 8, 10, 11]
    kfmt = lambda v: f"{v:g} kHz" if v == 11 else f"{v:g}"  # noqa: E731
    top = Panel(80, 112, 640, 170, 0, 11, -16, 1)
    s.text(32, top.y - 6, "tone out, dB", 11, theme.muted)
    s.grid(top, [0, -5, -10, -15], lambda v: minus(f"{v:g}"), xt, kfmt)
    s.series(top, list(zip(hz, wg)), theme.series2)
    s.series(top, list(zip(hz, tg)), theme.series1)
    px, py = top.px(10), top.py(wg[at10])
    s.dot(px, py, theme.series2)
    s.text(px - 10, py + 4, minus(f"{wg[at10]:.1f} dB at 10 kHz"), 12, theme.primary, "end")
    s.dot(top.px(10), top.py(tg[at10]), theme.series1)
    s.text(top.px(10) - 10, top.py(tg[at10]) - 8, minus(f"{tg[at10]:.4f} dB at 10 kHz"), 12, theme.primary, "end")
    bottom = Panel(80, 340, 640, 170, 0, 11, -135, 0)
    s.text(32, bottom.y - 6, "loudest image, dB", 11, theme.muted)
    s.grid(bottom, [0, -40, -80, -120], lambda v: minus(f"{v:g}"), xt, kfmt)
    s.series(bottom, list(zip(hz, wi)), theme.series2)
    s.series(bottom, list(zip(hz, ti)), theme.series1)
    s.dot(bottom.px(hz[-1]), bottom.py(wi[-1]), theme.series2)
    s.text(bottom.px(hz[-1]) - 10, bottom.py(wi[-1]) - 8, minus(f"{wi[-1]:.0f} dB at 11 kHz"), 12, theme.primary,
           "end")
    s.text(bottom.px(5.5), bottom.py(max(ti)) - 10, minus(f"transparent: {max(ti):.0f} dB or lower (the "
           "measurement's floor)"), 12, theme.primary, "middle")
    s.text(bottom.x + bottom.w / 2, height - 14, "tone frequency (kHz)", 11, theme.muted, "middle")
    s.write(OUT / f"converters-{theme.name}.svg")


def bitexact(theme: Theme, excerpt: Path, difference: Path) -> None:
    """A recorded Windows render, this library's replay of it and their difference."""
    ex = list(csv.DictReader(open(excerpt)))
    df = list(csv.DictReader(open(difference)))
    t0 = float(ex[0]["ms"])
    ms = [float(r["ms"]) - t0 for r in ex]
    win = [int(r["windows"]) for r in ex]
    lib = [int(r["library"]) for r in ex]
    secs = [float(r["seconds"]) for r in df]
    worst = max(int(r["max_abs_difference"]) for r in df)
    song = secs[-1] + 0.5
    width, height = 760, 560
    s = Svg(width, height, theme, "This library replays the Windows synth sample for sample",
            f"A recording of the Windows synth (Windows Server 2022) and this library replaying the same events "
            f"and render calls: the waveforms are the same and the largest difference over the whole "
            f"{song:.0f} s song is {worst}.")
    s.text(32, 36, "This library replays the Windows synth sample for sample", 17, theme.primary, weight=600)
    s.text(32, 58, "The Windows synth recorded on Windows Server 2022, and this library given the same events and "
                   "render calls.", 12, theme.secondary)
    lim = 32768  # the 16-bit range
    yt = [20000, 0, -20000]
    yl = lambda v: minus(f"{v / 1000:g}k") if v else "0"  # noqa: E731
    xt = [0, 5, 10, 15, 20]
    for i, (name, data, ink) in enumerate((("Windows (recorded), left channel", win, theme.series1),
                                           ("this library (replayed), left channel", lib, theme.series2))):
        p = Panel(80, 104 + i * 150, 640, 100, 0, 20, -lim, lim)
        s.text(32, p.y - 10, name, 12, theme.primary, weight=600)
        s.grid(p, yt, yl, xt, lambda v: f"{v:g} ms" if v == 20 else f"{v:g}")
        s.series(p, list(zip(ms, data)), ink)
    p = Panel(80, 424, 640, 70, 0, song, -1, 1)
    s.text(32, p.y - 10, f"largest |difference| in each 0.5 s, the whole {song:.0f} s song", 12, theme.primary,
           weight=600)
    s.grid(p, [1, 0, -1], lambda v: minus(f"{v:g}"), [0, 30, 60, 90, 120, 140],
           lambda v: f"{v:g} s" if v == 140 else f"{v:g}")
    s.series(p, [(x, int(r["max_abs_difference"])) for x, r in zip(secs, df)], theme.primary)
    samples = sum(int(r["samples"]) for r in df)
    differ = "0" if worst == 0 else "some"
    s.text(p.x + p.w, p.y - 10, f"{differ} of {samples:,} samples differ".replace(",", " "), 12, theme.primary, "end")
    s.text(p.x + p.w / 2, height - 14, f"excerpt: 20 ms from {t0 / 1000:.2f} s into the song; "
                                       "difference in 16-bit sample steps", 11, theme.muted, "middle")
    s.write(OUT / f"bitexact-{theme.name}.svg")


def main() -> None:
    """Command-line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--excerpt", type=Path)
    parser.add_argument("--difference", type=Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for theme in (LIGHT, DARK):
        converters(theme)
        if args.excerpt and args.difference:
            bitexact(theme, args.excerpt, args.difference)
    print(f"charts written to {OUT}")


if __name__ == "__main__":
    main()
