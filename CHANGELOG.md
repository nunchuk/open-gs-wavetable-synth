# Changelog

All notable changes to this project are documented here, following the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) style.

## [Unreleased]

## [0.1.0] - 2026-11-13

The first release of Open GS: the Microsoft GS Wavetable Synth (MSGS) of Windows, reimplemented in
C++20 and bit-exact. 52 recordings of the Windows synth (64.0 M frames) replay with 0 differing
samples, and Linux, macOS, Windows and WebAssembly render the same samples.

- `gswavetable-render gm.dls song.mid song.wav` renders a MIDI file at 22 050 Hz, or at 48 kHz with
  `--48k` (transparent) or `--48k windows` (as Windows 10 converts it).
- A C++20 library with a CMake package (`FetchContent`, `find_package`) and examples.

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
- Two 22 050 -> 48 000 Hz converters. `TransparentResampler`, the default: a Kaiser-windowed sinc
  cut at 10 762.5 Hz, 140 input frames each side, linear phase (tones to 10.5 kHz within
  0.0003 dB, 11 kHz at -75.8 dB, every image -103.8 dB or below), equal to the game port's `hq`
  converter to the bit when both are built without floating-point contraction (13.7 M samples of
  a song, 0 differ). `WindowsResampler`: Windows 10's audio engine's converter, identified from
  loopback recordings (to -144.8 dB, float precision). Why the transparent one is the default:
  Windows 10's converter is a host effect that dulls the synth. On four MIDI files matched in
  loudness it puts the 10 kHz third octave 4.9-5.7 dB under a transparent conversion (the same
  within 0.1 dB below 6 kHz) and leaves images above 11 kHz at -43..-49 dB of the energy; per
  instrument that is about ten times the difference between this engine and slopgs. Windows XP,
  whose `gm.dls` is the same file, sent the 22 050 Hz stream to a 22 050 Hz card unconverted, and
  listening found those recordings right where the Windows 10 conversion sounded dull (the game
  port's measurements, 2026-11-11). The game port's NEON block path is not taken: it relies on
  clang fusing the scalar sums into multiply-adds, which `-ffp-contract=off` forbids; without it
  the converter costs about 0.34 % of real time on an M4 Mac.
- `gswavetable-render`: renders a MIDI file to WAV, from a tick (`--from`), at 22 050 Hz or at
  48 kHz (`--48k`, transparent; `--48k windows`). The WAV ends at the file's end frame.
- Tests that need no Windows files: the tables' hash (FNV-1a 64 `0xa71c7a8d3fdda710`), the
  sequencer on generated MIDI files, both converters' tone responses, and golden SHA-256 renders on a
  generated DLS bank (`tests/data/sine.dls`) of two probe files and `features.mid`, which covers
  the MIDI and SysEx handling and more notes than voices. With `GSWAVETABLE_DLS` the same goldens on
  `gm.dls`.
- CI on Linux (GCC, Clang), macOS (AppleClang), Windows (MSVC) and WebAssembly (Emscripten on
  Node.js), warnings as errors, so that the golden hashes are checked on every platform; each
  native job also installs the library and builds `examples/` against the installed package.
- `examples/render_wav.cpp` and `examples/audio_callback.cpp` (48 kHz float frames on demand, as an
  audio callback asks for them), built in CI. Why: agents and people copy the README's code, so it
  must compile and give the verified sound. ctest `readme_example` checks that README.md's C++
  block is `examples/render_wav.cpp` character for character, `example_render_wav` that it writes
  the golden render of `probe1.mid`, and `example_audio_callback` that the streamed 48 kHz frames
  equal `gswavetable-render --48k`'s, then silence. `.gitattributes` keeps text files LF so those
  byte comparisons hold on Windows checkouts.
- WebAssembly builds use WebAssembly's exception handling (`-fwasm-exceptions`, public on the
  library target). Why: `load_dls` and `Sequencer` report bad input with exceptions, and Emscripten
  catches none by default; the first CI run's sequencer test died with `UnhandledPromiseRejection:
  ... The promise rejected with the reason "71208"` (the thrown object's address) where it expects
  `std::invalid_argument`, and a program could not catch a bad file either. With it the tests pass
  on Node.js and the `gm.dls` goldens rendered there have the same SHA-256 as on every other
  platform.
- CMake install rules and a package config: `find_package(gswavetable 0.1 REQUIRED)` after
  `cmake --install` (`GSWAVETABLE_INSTALL`, on when this is the top-level project), which a package
  manager port needs.
- `.github/workflows/release.yml`: on a `v*` tag, builds and tests on Linux (libstdc++ linked
  statically), macOS (universal arm64 and x86_64), Windows (static CRT) and WebAssembly, and
  attaches the installed trees to the tag's GitHub Release, so that the CLI can be used without
  building it. The files are `opengs-macos.tar.gz`, `opengs-linux.tar.gz`, `opengs-windows.zip`
  and `opengs-wasm.tar.gz`, without a version, so that `releases/latest/download/<file>` always
  names the latest; the notes are the summary at the top of the version's section here, the
  downloads and a link to this file (the first v0.1.0 attached `gswavetable-0.1.0-<platform>` with
  the whole section as notes, too long to read and not named like the repository).
- README.md written for readers that search and compare, people and AI agents alike, under the
  project's short name, Open GS (the files of a release are `opengs-<platform>`): an
  at-a-glance table, a quick start, the evidence as charts with their numbers in text and tables
  (a recording of the Windows synth against this library's replay; the two 48 kHz converters'
  response, from `docs/charts/converter_response.cpp`), when not to use it, an FAQ, and the
  Windows about box of the synth with a trademark notice. `docs/charts/make_charts.py` writes the
  SVGs (light and dark) with a colour-vision-checked palette.
- AI collaboration rules: `AGENTS.md` (shared, with this project's build commands, code map and
  invariants), `CLAUDE.md` and a Kiro agent that load it.

### Notes

- The code comes from the author's game port, where it was verified against Windows. To show the
  extraction changed nothing: all 25 of that game's MIDI files and the probes, from the start and
  from five other positions, render byte for byte as the original renderer's output (34 renders;
  the original pads the last 256-frame block with silence, this CLI stops at the end frame), and
  9 928 random play, render, pause, resume and stop calls on the old and the new player gave the
  same output over 68.1 M frames. The source changes are the namespace, comments, explicit
  conversions where `-Wconversion` and MSVC `/W4` would warn, and a local variable renamed where it
  hid a member.
- The bit-exactness was checked again with this library itself, not the game port's copy: all 52
  recordings of the Windows synth (64 036 275 frames) replay with 0 differing samples.
- Built with `-ffp-contract=off` (MSVC `/fp:precise`): the tables use `pow`, `log10` and `sin`, and a
  fused multiply-add could flip a rounding. On AppleClang arm64 the output is the same with and
  without contraction; the tables test catches a platform where it is not. A universal macOS build
  run as x86_64 under Rosetta passes the tables test and all golden renders, `gm.dls`'s included.
