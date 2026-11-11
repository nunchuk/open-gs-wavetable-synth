#!/usr/bin/env python3
"""Write features.mid, a format 1 Standard MIDI File that exercises the engine's MIDI handling.

The probes (probe1.mid, probe2.mid) play one note at a time; this file covers what they do
not, so that the golden renders reach most of the engine's code on every platform:

    tempo changes; a GS reset, a GS rhythm part, GS scale tuning, master volume and GM System
    On SysEx; RPN pitch-bend range, fine and coarse tuning; pitch bend; the mod wheel on the
    LFO; expression, volume and pan sweeps; channel pressure; the sustain pedal; reset all
    controllers; all notes off, all sound off; mono and poly mode; bank select (a bank the
    bank lacks, then back); program changes during notes; a drum kit with velocity layers and
    an exclusive group; repeated notes; more notes at once than the synth's 40 voices; running
    status; and a note-on at the last tick, which must not sound.

Usage:
    python3 make_features_mid.py [OUTPUT]   (default: features.mid next to this script)
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

Q = 384  # ticks per quarter note
Event = tuple[int, bytes]  # absolute tick, message (a meta event or SysEx already framed)


def vlq(value: int) -> bytes:
    """Encode a variable-length quantity."""
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def tempo(tick: int, us_per_quarter: int) -> Event:
    """Return a set-tempo meta event."""
    return tick, b"\xff\x51\x03" + us_per_quarter.to_bytes(3, "big")


def sysex(tick: int, body: bytes) -> Event:
    """Return a SysEx event; `body` follows F0 and ends in F7."""
    return tick, b"\xf0" + vlq(len(body)) + body


def roland(tick: int, address: bytes, data: bytes) -> Event:
    """Return a Roland GS DT1 SysEx with its checksum."""
    checksum = (128 - sum(address + data) % 128) % 128
    return sysex(tick, b"\x41\x10\x42\x12" + address + data + bytes([checksum, 0xF7]))


def cc(tick: int, channel: int, controller: int, value: int) -> Event:
    """Return a control change."""
    return tick, bytes([0xB0 | channel, controller, value])


def note(tick: int, channel: int, key: int, velocity: int, length: int, off: bool = False) -> list[Event]:
    """Return a note-on and, `length` ticks later, a note-on with velocity 0 (or a note-off)."""
    end = bytes([0x80 | channel, key, 64]) if off else bytes([0x90 | channel, key, 0])
    return [(tick, bytes([0x90 | channel, key, velocity])), (tick + length, end)]


def program(tick: int, channel: int, number: int) -> Event:
    """Return a program change."""
    return tick, bytes([0xC0 | channel, number])


def bend(tick: int, channel: int, value: int) -> Event:
    """Return a pitch bend of 0..16383."""
    return tick, bytes([0xE0 | channel, value & 0x7F, value >> 7])


def rpn(tick: int, channel: int, number: int, msb: int, lsb: int | None = None) -> list[Event]:
    """Return an RPN data entry, then the null RPN."""
    out = [cc(tick, channel, 101, 0), cc(tick, channel, 100, number), cc(tick, channel, 6, msb)]
    if lsb is not None:
        out.append(cc(tick, channel, 38, lsb))
    return out + [cc(tick, channel, 101, 127), cc(tick, channel, 100, 127)]


END = 30 * Q


def conductor() -> list[Event]:
    """Track 0: tempo and system SysEx."""
    scale = bytes([0x40 + d for d in (0, 10, -5, 0, 7, -10, 0, 3, -3, 0, 12, -12)])
    return [
        tempo(0, 500000),
        roland(0, b"\x40\x00\x7f", b"\x00"),  # GS reset
        roland(1, b"\x40\x1a\x15", b"\x01"),  # part 11 (channel 11) is a rhythm part
        roland(2, b"\x40\x12\x40", scale),    # scale tuning of part 2 (channel 2)
        tempo(8 * Q, 600000),
        sysex(12 * Q, b"\x7f\x7f\x04\x01\x00\x60\xf7"),  # master volume
        tempo(16 * Q, 428571),
        sysex(26 * Q, b"\x7f\x7f\x04\x01\x00\x7f\xf7"),
        sysex(27 * Q, b"\x7e\x7f\x09\x01\xf7"),  # GM System On
        tempo(28 * Q, 500000),
    ]


def piano() -> list[Event]:
    """Track 1, channel 1: arpeggios, the sustain pedal, pitch bend, the mod wheel, tuning."""
    ch = 0
    out = [program(Q // 8, ch, 0), cc(Q // 8, ch, 7, 110), cc(Q // 8, ch, 10, 64)]
    out += rpn(Q // 8, ch, 0, 12)  # pitch-bend range: 12 semitones
    keys = [48, 52, 55, 60, 64, 67, 72, 76]
    for i in range(32):  # bars 1-2: 16th notes, velocities 40..120
        out += note(Q + i * Q // 4, ch, keys[i % 8], 40 + (i * 37) % 81, Q // 4)
    out.append(cc(3 * Q, ch, 64, 127))  # bar 3 held by the pedal
    for i in range(8):
        out += note(3 * Q + i * Q // 2, ch, keys[7 - i], 90, Q // 8)
    out.append(cc(7 * Q, ch, 64, 0))
    out += note(7 * Q, ch, 60, 100, 4 * Q)  # bars 4-5: bend up, down and back under one note
    for i in range(48):
        phase = i / 48
        value = round(8192 + 8191 * (4 * phase if phase < 0.25 else 2 - 4 * phase if phase < 0.75 else 4 * phase - 4))
        out.append(bend(7 * Q + i * Q // 12, ch, max(0, min(16383, value))))
    out.append(bend(11 * Q, ch, 8192))
    out += note(11 * Q, ch, 64, 100, 4 * Q)  # the mod wheel on the LFO (after its delay)
    for i in range(32):
        out.append(cc(11 * Q + i * Q // 8, ch, 1, min(127, i * 4)))
    out.append(cc(15 * Q, ch, 1, 0))
    out += rpn(15 * Q, ch, 2, 0x42)          # coarse tune +2 semitones
    out += rpn(15 * Q, ch, 1, 0x50, 0x00)    # fine tune +25 cents
    for i in range(8):
        out += note(15 * Q + i * Q // 2, ch, keys[i], 100, Q // 2)
    out += rpn(19 * Q, ch, 2, 0x40)
    out += rpn(19 * Q, ch, 1, 0x40, 0x00)
    out.append(cc(19 * Q, ch, 121, 0))  # reset all controllers (volume and pan kept)
    out += note(28 * Q, ch, 60, 100, Q)
    out += note(28 * Q, ch, 67, 100, Q)
    return out


def strings() -> list[Event]:
    """Track 2, channel 2: chords under swells, a pan sweep, channel pressure, the GS tuning."""
    ch = 1
    out = [program(0, ch, 48)]
    for start, chord in ((Q, (55, 59, 62)), (5 * Q, (57, 60, 64)), (9 * Q, (53, 57, 60, 65))):
        for key in chord:
            out += note(start, ch, key, 80, 4 * Q - Q // 4, off=True)
        for i in range(16):
            out.append(cc(start + i * Q // 4, ch, 11, 20 + i * 7))
            out.append(cc(start + i * Q // 4, ch, 10, (start // Q * 13 + i * 8) % 128))
            out.append((start + i * Q // 4, bytes([0xD0 | ch, i * 8])))
    out.append(cc(13 * Q, ch, 121, 1))  # reset all controllers, volume and pan too
    out.append(cc(13 * Q, ch, 7, 90))
    for key in (50, 57, 62, 66):
        out += note(13 * Q, ch, key, 70, 6 * Q, off=True)
    out.append(program(16 * Q, ch, 73))  # a program change under the held chord: new notes only
    out += note(16 * Q, ch, 74, 90, 2 * Q)
    out.append(cc(17 * Q, ch, 1, 100))   # the flute's mod wheel tremolo
    return out


def drums() -> list[Event]:
    """Track 3, channels 10 and 11: kick, snare layers, the hi-hat group, crashes."""
    out = []
    for bar in range(1, 13):
        base = bar * Q
        out += note(base, 9, 36, 110, Q // 8)
        out += note(base + Q // 2, 9, 38, 40 if bar % 2 else 100, Q // 8)
        for i in range(4):
            out += note(base + i * Q // 4, 9, 46 if i == 3 else 42, 70 + i * 10, Q // 8)
    for i in range(4):  # crashes one over another (self non-exclusive), a key without a region
        out += note(13 * Q + i * Q // 3, 9, 49, 100, Q // 4)
    out += note(14 * Q, 9, 90, 100, Q // 4)
    for i in range(4):  # the rhythm part on channel 11
        out += note(20 * Q + i * Q // 2, 10, 36 if i % 2 == 0 else 38, 100, Q // 8)
    return out


def ensemble() -> list[Event]:
    """Track 4, channels 3-8: more notes than voices, mono mode, sound off, bank select, the rest."""
    out = [program(0, 2, 16), program(0, 3, 11), program(0, 4, 56), program(0, 5, 24), program(0, 6, 33),
           program(0, 7, 73)]
    for i in range(24):  # 48 notes at once on 40 voices
        out += note(21 * Q, 2, 36 + 2 * i, 90, Q)
        out += note(21 * Q + 1, 3, 37 + 2 * i, 90, Q)
    out.append(cc(22 * Q, 2, 123, 0))  # all notes off, all sound off
    out.append(cc(22 * Q, 3, 120, 0))
    out.append(cc(22 * Q + Q // 2, 3, 126, 1))  # mono: each note ends the one before
    for i in range(6):
        out += note(22 * Q + Q // 2 + i * Q // 4, 3, 60 + i, 100, Q // 2)
    out.append(cc(24 * Q, 3, 127, 0))
    out += [cc(24 * Q, 2, 0, 8), program(24 * Q, 2, 16)]  # GS bank 8: not in the bank, silent
    out += note(24 * Q, 2, 60, 100, Q // 2)
    out += [cc(24 * Q + Q // 2, 2, 0, 0), program(24 * Q + Q // 2, 2, 16)]
    out += note(24 * Q + Q // 2, 2, 60, 100, Q // 2)
    for i, (ch, key) in enumerate(((4, 67), (5, 64), (6, 40), (7, 79))):  # trumpet, guitar, bass, flute
        for k in range(3):
            out += note(25 * Q + i * Q // 2 + k * Q // 6, ch, key + k * 2, 50 + 30 * k, Q // 3)
    out += note(25 * Q, 4, 67, 100, Q // 4)  # the same note again while it sounds
    out += note(25 * Q + Q // 8, 4, 67, 100, Q // 4)
    out.append((END, bytes([0x92, 72, 100])))  # at the last tick: never sounds
    return out


def track(events: list[Event], running_status: bool) -> bytes:
    """Encode events (sorted by tick, stable) as an MTrk chunk ending at END."""
    data = bytearray()
    last, status = 0, 0
    for tick, message in sorted(events, key=lambda e: e[0]):
        data += vlq(tick - last)
        last = tick
        if message[0] < 0xF0 and running_status and message[0] == status:
            data += message[1:]
        else:
            data += message
            status = message[0] if message[0] < 0xF0 else 0
    data += vlq(END - last) + b"\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(data)) + bytes(data)


def build() -> bytes:
    """Return the bytes of features.mid."""
    tracks = [track(conductor(), False), track(piano(), True), track(strings(), False), track(drums(), True),
              track(ensemble(), False)]
    return b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), Q) + b"".join(tracks)


def main() -> None:
    """Command-line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("output", type=Path, nargs="?", default=Path(__file__).with_name("features.mid"))
    args = parser.parse_args()
    data = build()
    args.output.write_bytes(data)
    print(f"{args.output}: {len(data)} bytes")


if __name__ == "__main__":
    main()
