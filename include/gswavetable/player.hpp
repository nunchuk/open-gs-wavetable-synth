// SPDX-License-Identifier: MIT
// A MIDI file played the way Windows plays it through MCI: the MCI sequencer (sequencer.hpp)
// sending the file's messages to the Microsoft GS Wavetable synth (engine.hpp) on its MIDI port.
// Every event comes at its exact tempo-map frame and the engine still renders in Windows' 10 ms
// cycles (221 and 220 frames, the WASAPI period that calls Render), so its control spans,
// envelopes, voice reserve and mix run as they do on Windows. Every play opens the MIDI port (a
// fresh synth) and the end of the file closes it, at the end's frame. The host's latencies are
// left out: a play starts at the next render cycle, not 0.25-0.3 s later.
//
// A Player may be used by one thread at a time.
#pragma once

#include "gswavetable/engine.hpp"
#include "gswavetable/sequencer.hpp"

#include <cstdint>
#include <memory>

namespace gswavetable {

class Player {
  public:
    explicit Player(std::shared_ptr<const Bank> bank);
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    // "play mid from <tick> notify": the port opens (a fresh synth at the next render cycle) and
    // `song` plays from `tick`: the chase (Sequencer::schedule) at once, then the file's events.
    void play(Sequencer song, std::uint32_t tick = 0);
    void stop();   // "stop mid": the port closes, the output ends
    void pause();  // "pause mid": AllNotesOff, the port closes, the schedule waits
    void resume(); // a fresh synth chased to the paused frame, the schedule goes on
    // The synth's 22 050 Hz stereo output, 2 * `frames` samples into `stereo`. Returns the frames
    // the synth rendered; the rest is silence (the port is closed: the end, a stop or a pause).
    std::uint32_t render(std::int16_t *stereo, std::uint32_t frames);
    [[nodiscard]] bool playing() const noexcept;           // the port is open: the synth renders
    [[nodiscard]] bool ended() const noexcept;             // the end came (the notify); the port is closed
    [[nodiscard]] std::uint32_t position() const noexcept; // "status mid position", in ticks

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gswavetable
