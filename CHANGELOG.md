# Changelog

All notable changes to this project are documented here, following the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) style.

## [Unreleased]

The first release, to be tagged 0.1.0.

### Added

- The synth (`engine.hpp`: `load_dls`, `Synth`): the DirectMusic synth core of Windows'
  `SysWOW64\wdmaud.drv` 10.0.20348.2849 reimplemented from its disassembly and public PDB. Evidence
  that it is bit-exact: replaying recordings of the Windows synth (its `PlayBuffer` events and
  `Render` calls, Windows Server 2022) gives 0 differing samples in 35 recordings of 25 MIDI files
  (58 448 809 frames) and in 17 recordings of seeks, chases and pauses (5 587 466 frames); its
  tables equal the 1 884 entries the driver computes (read from its `.data`).
- `Sequencer` and `Player`: a Standard MIDI File sent the way `mciseq.dll` sends it (order, chase,
  note-off before a repeated note-on), each event at its exact tempo-map frame, rendered in
  Windows' 10 ms cycles. `Player` is an object (play, stop, pause, resume, render, playing, ended,
  position in ticks) so that a program can run several; `render` returns the frames of music.
- `Resampler`: Windows' 22 050 -> 48 000 Hz converter, identified from loopback recordings (to
  -144.8 dB, float precision).
- `gswavetable-render`: renders a MIDI file to WAV, from a tick (`--from`), at 22 050 Hz or through
  the converter at 48 kHz (`--48k`). The WAV ends at the file's end frame.
- Tests that need no Windows files: the tables' hash (FNV-1a 64 `0xa71c7a8d3fdda710`), the
  sequencer on generated MIDI files, the converter's tone response, and golden SHA-256 renders on a
  generated DLS bank (`tests/data/sine.dls`) of two probe files and `features.mid`, which covers
  the MIDI and SysEx handling and more notes than voices. With `GSWAVETABLE_DLS` the same goldens on
  `gm.dls`.
- CI on Linux (GCC, Clang), macOS (AppleClang), Windows (MSVC) and WebAssembly (Emscripten on
  Node.js), warnings as errors, so that the golden hashes are checked on every platform.

### Notes

- The code comes from the author's game port, where it was verified against Windows. To show the
  extraction changed nothing: all 25 of that game's MIDI files and the probes, from the start and
  from five other positions, render byte for byte as the original renderer's output (34 renders;
  the original pads the last 256-frame block with silence, this CLI stops at the end frame), and
  9 928 random play, render, pause, resume and stop calls on the old and the new player gave the
  same output over 68.1 M frames. The source changes are the namespace, comments, explicit
  conversions where `-Wconversion` and MSVC `/W4` would warn, and a local variable renamed where it
  hid a member.
- Built with `-ffp-contract=off` (MSVC `/fp:precise`): the tables use `pow`, `log10` and `sin`, and a
  fused multiply-add could flip a rounding. On AppleClang arm64 the output is the same with and
  without contraction; the tables test catches a platform where it is not.
