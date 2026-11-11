#!/usr/bin/env python3
"""Write sine.dls, a small DLS level 1 bank of sine-based waves for the golden tests.

The bank stands in for gm.dls where gm.dls cannot be shipped (CI, the tests in this
repository). It holds an instrument for every program the probes play (0, 11, 16, 24, 33,
48, 56, 73) and a drum kit, with articulations chosen to exercise the engine's paths:
volume and pitch envelopes, the LFO on pitch and on volume, the mod wheel, velocity to
attack, key to decay, pan, region attenuation, key and velocity splits, an exclusive key
group, 8- and 16-bit waves, loops from the wave's start and from inside it, and one-shots.

Waves (all mono PCM):
    0  16-bit, 44 100 Hz, 100-sample period (441 Hz), 400 samples, looped whole
    1   8-bit, 22 050 Hz,  50-sample period (441 Hz), 200 samples, looped whole
    2  16-bit, 22 050 Hz, 147 Hz sine decaying over 3 307 samples, never looped
    3  16-bit, 22 050 Hz, fundamental plus second harmonic, 441 Hz, 1 000 samples: the
       first 500 decay to half level, the last 500 are the loop

The committed sine.dls is the reference; the goldens are hashes of renders with it.

Usage:
    python3 make_sine_dls.py [OUTPUT]   (default: sine.dls next to this script)
"""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path
from typing import NamedTuple

# DLS level 1 connection sources, controls and destinations (dls1.h).
SRC_NONE, SRC_LFO, SRC_KEYONVELOCITY, SRC_KEYNUMBER, SRC_EG2 = 0, 1, 2, 3, 5
SRC_CC1 = 0x81
DST_ATTENUATION, DST_PITCH, DST_PAN = 1, 3, 4
DST_LFO_FREQUENCY, DST_LFO_STARTDELAY = 0x104, 0x105
DST_EG1_ATTACKTIME, DST_EG1_DECAYTIME = 0x206, 0x207
DST_EG1_RELEASETIME, DST_EG1_SUSTAINLEVEL = 0x209, 0x20A
DST_EG2_ATTACKTIME, DST_EG2_DECAYTIME = 0x30A, 0x30B
DST_EG2_RELEASETIME, DST_EG2_SUSTAINLEVEL = 0x30D, 0x30E

DRUMS = 0x80000000  # F_INSTRUMENT_DRUMS in the locale's bank
WLNK_CHANNEL_LEFT = 1
RGN_OPTION_SELFNONEXCLUSIVE = 1

Connection = tuple[int, int, int, int]  # source, control, destination, scale


def chunk(fourcc: bytes, body: bytes) -> bytes:
    """Return a RIFF chunk, padded to an even size."""
    return fourcc + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def riff_list(kind: bytes, list_type: bytes, *children: bytes) -> bytes:
    """Return a RIFF or LIST chunk of the given type holding the children."""
    return chunk(kind, list_type + b"".join(children))


def time_cents(seconds: float) -> int:
    """Return a DLS time in time cents (65536ths); 0 s is the 0x80000000 'no time' value."""
    if seconds <= 0:
        return -0x80000000
    return round(1200 * math.log2(seconds) * 65536)


def lfo_frequency(hz: float) -> int:
    """Return an LFO frequency as DLS absolute pitch cents (65536ths)."""
    return round((1200 * math.log2(hz / 440) + 6900) * 65536)


def units(value: float) -> int:
    """Return a scale in whole units (cents, 0.1 % or centibels) as 65536ths."""
    return round(value * 65536)


class Envelope(NamedTuple):
    """An EG1 (volume) or EG2 (pitch) envelope in seconds and 0.1 % of full level."""

    attack: float
    decay: float
    sustain: int
    release: float


def envelope(eg: Envelope, pitch: bool) -> list[Connection]:
    """Return the connections of one envelope."""
    a, d, s, r = ((DST_EG2_ATTACKTIME, DST_EG2_DECAYTIME, DST_EG2_SUSTAINLEVEL, DST_EG2_RELEASETIME)
                  if pitch else
                  (DST_EG1_ATTACKTIME, DST_EG1_DECAYTIME, DST_EG1_SUSTAINLEVEL, DST_EG1_RELEASETIME))
    return [(SRC_NONE, 0, a, time_cents(eg.attack)), (SRC_NONE, 0, d, time_cents(eg.decay)),
            (SRC_NONE, 0, s, units(eg.sustain)), (SRC_NONE, 0, r, time_cents(eg.release))]


def art1(connections: list[Connection]) -> bytes:
    """Return an lart list holding one art1 chunk."""
    body = struct.pack("<II", 8, len(connections))
    for source, control, destination, scale in connections:
        body += struct.pack("<HHHHi", source, control, destination, 0, scale)
    return riff_list(b"LIST", b"lart", chunk(b"art1", body))


class Region(NamedTuple):
    """One region: a key and velocity range on a wave, its loop and articulation."""

    keys: tuple[int, int]
    wave: int
    unity: int
    fine_tune: int = 0
    attenuation_cb: int = 0  # centibels, negative = quieter
    loop: tuple[int, int] | None = None  # start, length
    velocities: tuple[int, int] = (0, 127)
    key_group: int = 0
    options: int = 0
    connections: list[Connection] | None = None  # the region's own articulation


def region(r: Region) -> bytes:
    """Return a 'rgn ' list."""
    rgnh = struct.pack("<HHHHHH", *r.keys, *r.velocities, r.options, r.key_group)
    loops = [r.loop] if r.loop else []
    wsmp = struct.pack("<IHhiII", 20, r.unity, r.fine_tune, units(r.attenuation_cb), 0, len(loops))
    for start, length in loops:
        wsmp += struct.pack("<IIII", 16, 0, start, length)
    wlnk = struct.pack("<HHII", 0, 0, WLNK_CHANNEL_LEFT, r.wave)
    children = [chunk(b"rgnh", rgnh), chunk(b"wsmp", wsmp), chunk(b"wlnk", wlnk)]
    if r.connections:
        children.append(art1(r.connections))
    return riff_list(b"LIST", b"rgn ", *children)


class Instrument(NamedTuple):
    """One instrument: its bank and program, regions and instrument-level articulation."""

    name: str
    bank: int
    program: int
    regions: list[Region]
    connections: list[Connection] | None = None


def instrument(ins: Instrument) -> bytes:
    """Return an 'ins ' list."""
    insh = struct.pack("<III", len(ins.regions), ins.bank, ins.program)
    children = [chunk(b"insh", insh),
                riff_list(b"LIST", b"lrgn", *(region(r) for r in ins.regions))]
    if ins.connections:
        children.append(art1(ins.connections))
    children.append(riff_list(b"LIST", b"INFO", chunk(b"INAM", ins.name.encode() + b"\0")))
    return riff_list(b"LIST", b"ins ", *children)


def pcm16(samples: list[float]) -> bytes:
    """Return samples in -1..1 as 16-bit little-endian PCM."""
    return struct.pack(f"<{len(samples)}h", *(round(s * 32767) for s in samples))


def pcm8(samples: list[float]) -> bytes:
    """Return samples in -1..1 as unsigned 8-bit PCM."""
    return bytes(128 + round(s * 127) for s in samples)


def wave(rate: int, bits: int, data: bytes) -> bytes:
    """Return a 'wave' list of mono PCM."""
    align = bits // 8
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * align, align, bits)
    return riff_list(b"LIST", b"wave", chunk(b"fmt ", fmt), chunk(b"data", data))


def waves() -> list[bytes]:
    """Return the four waves described in the module docstring."""
    sine0 = [0.8 * math.sin(2 * math.pi * i / 100) for i in range(400)]
    sine1 = [0.8 * math.sin(2 * math.pi * i / 50) for i in range(200)]
    decay = [0.9 * math.exp(-i / 800) * math.sin(2 * math.pi * 147 * i / 22050) for i in range(3307)]
    harmonic = []
    for i in range(1000):
        level = 1.0 - 0.5 * min(i, 500) / 500
        phase = 2 * math.pi * i / 50
        harmonic.append(level * (0.55 * math.sin(phase) + 0.3 * math.sin(2 * phase)))
    return [wave(44100, 16, pcm16(sine0)), wave(22050, 8, pcm8(sine1)),
            wave(22050, 16, pcm16(decay)), wave(22050, 16, pcm16(harmonic))]


LOOP0, LOOP1, LOOP3 = (0, 400), (0, 200), (500, 500)


def instruments() -> list[Instrument]:
    """Return the bank's instruments."""
    mod_wheel_vibrato = [(SRC_LFO, SRC_CC1, DST_PITCH, units(50))]
    return [
        Instrument("Piano", 0, 0, [
            Region((0, 59), 3, 69, -4, loop=LOOP3),
            Region((60, 127), 0, 69, -4, loop=LOOP0),
        ], envelope(Envelope(0.005, 1.5, 0, 0.3), False) + [
            (SRC_KEYONVELOCITY, 0, DST_EG1_ATTACKTIME, units(-1200)),
            (SRC_KEYNUMBER, 0, DST_EG1_DECAYTIME, units(-1200)),
            (SRC_NONE, 0, DST_LFO_FREQUENCY, lfo_frequency(5.0)),
            (SRC_NONE, 0, DST_LFO_STARTDELAY, time_cents(0.5)),
        ] + mod_wheel_vibrato),
        Instrument("Vibraphone", 0, 11, [Region((0, 127), 0, 69, -4, loop=LOOP0)],
                   envelope(Envelope(0.0, 2.5, 0, 0.6), False) + [
                       (SRC_NONE, 0, DST_LFO_FREQUENCY, lfo_frequency(4.0)),
                       (SRC_LFO, 0, DST_ATTENUATION, units(20)),
                   ]),
        Instrument("Organ", 0, 16, [Region((0, 127), 1, 69, -4, loop=LOOP1)],
                   envelope(Envelope(0.0, 0.0, 1000, 0.05), False)),
        Instrument("Nylon Guitar", 0, 24, [
            Region((0, 127), 3, 69, -4, loop=LOOP3, connections=envelope(Envelope(0.002, 2.0, 0, 0.2), False)
                   + envelope(Envelope(0.0, 0.05, 0, 0.05), True)
                   + [(SRC_EG2, 0, DST_PITCH, units(100))]),
        ]),
        Instrument("Bass", 0, 33, [Region((0, 127), 0, 69, -4, loop=LOOP0)],
                   envelope(Envelope(0.003, 1.0, 400, 0.1), False)),
        Instrument("Strings", 0, 48, [Region((0, 127), 3, 69, -4, loop=LOOP3)],
                   envelope(Envelope(0.3, 1.0, 800, 0.8), False) + [
                       (SRC_NONE, 0, DST_LFO_FREQUENCY, lfo_frequency(5.5)),
                       (SRC_NONE, 0, DST_LFO_STARTDELAY, time_cents(0.2)),
                       (SRC_LFO, 0, DST_PITCH, units(8)),
                   ] + mod_wheel_vibrato),
        Instrument("Trumpet", 0, 56, [Region((0, 127), 1, 69, -4, loop=LOOP1)],
                   envelope(Envelope(0.03, 0.3, 700, 0.15), False)
                   + envelope(Envelope(0.02, 0.08, 0, 0.1), True) + [
                       (SRC_EG2, 0, DST_PITCH, units(-50)),
                       (SRC_KEYONVELOCITY, 0, DST_EG2_ATTACKTIME, units(-600)),
                       (SRC_KEYNUMBER, 0, DST_EG2_DECAYTIME, units(300)),
                   ]),
        Instrument("Flute", 0, 73, [Region((0, 127), 0, 69, -4, attenuation_cb=-30, loop=LOOP0)],
                   envelope(Envelope(0.08, 0.5, 900, 0.25), False) + [
                       (SRC_NONE, 0, DST_PAN, units(-300)),
                       (SRC_NONE, 0, DST_LFO_FREQUENCY, lfo_frequency(5.0)),
                       (SRC_LFO, 0, DST_PITCH, units(15)),
                       (SRC_LFO, SRC_CC1, DST_ATTENUATION, units(30)),
                   ]),
        Instrument("Drums", DRUMS, 0, [
            Region((35, 37), 2, 36, attenuation_cb=-20),
            Region((38, 40), 2, 30, velocities=(0, 63), attenuation_cb=-60),
            Region((38, 40), 2, 30, velocities=(64, 127)),
            Region((42, 42), 0, 30, key_group=1, loop=LOOP0,
                   connections=envelope(Envelope(0.0, 0.08, 0, 0.03), False)),
            Region((44, 46), 0, 32, key_group=1, loop=LOOP0,
                   connections=envelope(Envelope(0.0, 0.6, 0, 0.1), False)),
            Region((47, 81), 3, 69, -4, options=RGN_OPTION_SELFNONEXCLUSIVE,
                   connections=envelope(Envelope(0.0, 1.2, 0, 1.2), False) + [(SRC_NONE, 0, DST_PAN, units(250))]),
        ], envelope(Envelope(0.0, 0.0, 1000, 0.5), False)),
    ]


def build() -> bytes:
    """Return the bytes of sine.dls."""
    ins = instruments()
    pool = waves()
    offsets, at = [], 0
    for w in pool:
        offsets.append(at)
        at += len(w)
    ptbl = struct.pack(f"<II{len(offsets)}I", 8, len(offsets), *offsets)
    return riff_list(b"RIFF", b"DLS ",
                     chunk(b"colh", struct.pack("<I", len(ins))),
                     chunk(b"vers", struct.pack("<II", 0x00010000, 0)),
                     riff_list(b"LIST", b"lins", *(instrument(i) for i in ins)),
                     chunk(b"ptbl", ptbl),
                     riff_list(b"LIST", b"wvpl", *pool),
                     riff_list(b"LIST", b"INFO", chunk(b"INAM", b"open-gs-wavetable-synth test bank\0")))


def main() -> None:
    """Command-line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("output", type=Path, nargs="?", default=Path(__file__).with_name("sine.dls"))
    args = parser.parse_args()
    data = build()
    args.output.write_bytes(data)
    print(f"{args.output}: {len(data)} bytes")


if __name__ == "__main__":
    main()
