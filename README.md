# open-gs-wavetable-synth

A bit-exact, portable C++20 reimplementation of the **Microsoft GS Wavetable Synth** (MSGS), the
MIDI synthesizer built into Windows, so that MIDI files played with `gm.dls` on **macOS, iOS,
Linux, Windows and the Web (WebAssembly)** sound as they do on Windows. The synth is bit-exact:
given the same MIDI events at the same sample times and the same render calls, its output equals
the Windows synth's, sample for sample. Replaying 35 recordings of the Windows synth (58.4 M
frames, Windows Server 2022) gives 0 differing samples. No dependencies beyond the C++ standard
library; MIT licensed.

What it reimplements: the DirectMusic synth core (`CSynth`, `CControlLogic`, `CVoice`, `CVoiceEG`,
`CVoiceLFO`, `CDigitalAudio`, `CInstManager` and the DLS loader) linked into
`C:\Windows\SysWOW64\wdmaud.drv` 10.0.20348.2849, the driver a 32-bit program uses, reconstructed
from its disassembly with Microsoft's public symbols. On top of it: a Standard MIDI File player
that sends messages the way Windows' MCI sequencer (`mciseq.dll`) does, and Windows' audio-engine
converter from the synth's 22 050 Hz to 48 kHz.

## Use it from CMake

```cmake
include(FetchContent)
FetchContent_Declare(gswavetable
  GIT_REPOSITORY https://github.com/nunchuk/open-gs-wavetable-synth.git
  GIT_TAG main) # pin a release tag or a commit for reproducible builds
FetchContent_MakeAvailable(gswavetable)
target_link_libraries(your_app PRIVATE gswavetable::gswavetable)
```

As a subproject it builds only the library (`add_subdirectory` works the same way); the CLI and
the tests are built when this is the top-level project (`GSWAVETABLE_BUILD_TOOLS`,
`GSWAVETABLE_BUILD_TESTS`).

## Render a MIDI file

```cpp
#include <gswavetable/player.hpp>

#include <fstream>
#include <iterator>
#include <vector>

static std::vector<std::uint8_t> read(const char *path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

int main() {
    // Throws std::runtime_error if the synth would reject the bank, std::invalid_argument for a bad MIDI file.
    gswavetable::Player player(gswavetable::load_dls(read("gm.dls")));
    player.play(gswavetable::Sequencer(read("song.mid"))); // from tick 0; play(song, tick) seeks
    std::vector<std::int16_t> pcm, block(2 * 1024);         // 22 050 Hz, 16-bit, stereo interleaved
    while (player.playing()) {                              // until the file's end
        const auto frames = player.render(block.data(), 1024);
        pcm.insert(pcm.end(), block.begin(), block.begin() + 2 * std::ptrdiff_t(frames));
    }
}
```

`Player::render` can also feed an audio callback directly: it fills the buffer it is given and
returns how many frames are music (the rest is silence once the file has ended). `pause()`,
`resume()`, `stop()` and `position()` (in ticks) work as with MCI. For 48 kHz output, push the
samples through `gswavetable::Resampler` (`resampler.hpp`). To drive the synth with your own
events instead of a file, use `gswavetable::Synth` (`engine.hpp`): `play(sample_time, message)`
and `render(buffer, frames, position)`.

## Command line

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
build/gswavetable-render gm.dls song.mid song.wav              # 16-bit PCM, 22 050 Hz
build/gswavetable-render --48k gm.dls song.mid song-48k.wav    # 32-bit float, 48 000 Hz
build/gswavetable-render --from 1920 gm.dls song.mid tail.wav  # from tick 1920, with the chase
```

The WAV ends where the file ends (its last tick), where Windows closes the MIDI port.

## Where to get gm.dls

`gm.dls` (the Roland GS sound set licensed to Microsoft) is part of Windows and is not included
here. Copy it from a Windows installation: `C:\Windows\System32\drivers\gm.dls`. The copy this
engine was verified with is Windows Server 2022's: 3 440 660 bytes, SHA-256
`3229b09b9d7d9f3f4793b0d9b34fe6abc75cfa4a2503c0c90f43ff651ba7f2c0`. The tests run without it, on
a small generated bank (`tests/data/sine.dls`).

## What it does, and does not, do

- Output: 22 050 Hz, 16-bit stereo, the only rate the Windows synth opens with.
- Voices: 32, plus 8 kept in reserve by quick-stopping the oldest notes, with Windows' voice
  stealing, priorities, exclusive key groups and same-note handling.
- No reverb and no chorus (Windows opens the synth without them). The DLS2 filter is not
  implemented: DLS1 banks such as `gm.dls` never enable it. Playing a wave of 512 Ki frames or more
  ("big-sample mode") throws `std::runtime_error` from `render`.
- MIDI: note on/off, program change, pitch bend; CC1 (modulation, as deep as the bank's
  articulation makes it), 7, 10, 11, 64, 120, 121, 123, 126/127 (mono/poly); RPN 0 (bend range),
  1 (fine tune) and 2 (coarse tune) through CC101/100 and data entry CC6/38; bank select CC0/32,
  ignored until a GS reset as on Windows. CC74, CC91, CC93 and channel pressure are recorded with
  no audible effect (no filter, no effects, no pressure depth in DLS1); an NRPN (CC98/99) only
  cancels the RPN. SysEx: GS reset, GS part to channel, GS rhythm part, GS scale tuning,
  XG System On, GM System On, master volume.
- Mixing: the x86 MMX path Windows uses on any current CPU (spans of more than 8 frames through
  `CDigitalAudio::Mix16X`, shorter ones through `Mix16`), including its saturation and rounding.
- Timing: the player renders in Windows' 10 ms cycles (221 and 220 frames, the WASAPI period), so
  envelopes, control spans and the voice reserve run as on Windows. The messages and their order
  are `mciseq.dll`'s (tracks merged in tick, track and file order; a note-off before a note-on for
  a note that is on; on a seek, every earlier event except note-ons). Their times are not:
  `mciseq` truncates each event's time to whole milliseconds and sends it from a timer (drifting by
  up to 1.4 s over a long file), while this player puts each event on the exact tempo-map time,
  rounded to the nearest frame (within 0.511 frames, nothing accumulates). A play starts at the
  next render cycle, without Windows' 0.25-0.3 s start-up latency.
- Threads: each `Synth` or `Player` may be used by one thread at a time. A loaded bank is
  immutable and may be shared by synths on different threads.

## How it was verified

On Windows Server 2022 a 32-bit program played the MIDI files through MCI while
`CUserModeSynth::PlayBuffer` (every event with its sample time) and `CUserModeSynth::Render` (every
buffer the synth produced) were hooked and recorded. Replaying the recorded events and Render calls
through this engine reproduces every sample:

- 35 recordings of 25 MIDI files played from the start or restored mid-file: 58 448 809 frames,
  0 differing samples;
- 17 recordings of seeks, chases and pauses: 5 587 466 frames, 0 differing samples;
- the engine's tables (1 884 entries) equal the ones the driver computes, read from its `.data`.

The 48 kHz converter was identified from loopback recordings of a 48 kHz endpoint fed by the same
plays (a 320/147 polyphase Kaiser-windowed sinc, 12 taps); it reproduces the loopback to float
precision (-144.8 dB on held-out data).

In this repository, CI (Linux GCC and Clang, macOS AppleClang, Windows MSVC, Emscripten on
Node.js) checks that every platform renders the golden files to the same SHA-256
(`tests/data/golden.tsv`).

## Known limitations

- Only the x86 driver's MMX path was checked against Windows. The x64 driver
  (`System32\wdmaud.drv`, used by 64-bit programs) and Windows 11 were not compared.
- Only `gm.dls` (the SHA-256 above) was checked; other DLS banks load, but were not compared with
  Windows.
- The player's event timing is the exact tempo map, not `mciseq`'s millisecond timer (see Timing),
  so a recording of Windows playing a file through MCI differs from this player's output in event
  timing, while the synth itself is identical.

## License

MIT, see [LICENSE](LICENSE). `gm.dls` is not covered and not distributed.
