// SPDX-License-Identifier: MIT
// A Standard MIDI File read the way Windows' MCI sequencer (mciseq.dll) reads it, for the Player
// (player.hpp) to send to the synth (engine.hpp).
//
// The messages are mciseq's: the tracks merged in (tick, track, file order), a note-off before a
// note-on for a note that is on, the chase of a play from a position. Their times are not:
// mciseq keeps every event's time in whole milliseconds with the tempo per tick truncated (a drift
// of up to 1.4 s by the end of a long file) and sends them from a timer onto the synth's 10 ms
// render cycles. Here every event is stamped with its time on the file's tempo map, in
// microseconds and then to the nearest 22 050 Hz frame (at most 0.511 frames off the exact time,
// nothing accumulates), and the engine starts its notes at that frame within the render.
#pragma once

#include "gswavetable/engine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace gswavetable {

class Sequencer {
  public:
    // Throws std::invalid_argument for a file that is not a format 0/1 SMF with ticks per beat.
    explicit Sequencer(std::span<const std::uint8_t> smf);

    struct Message {
        std::uint32_t size;              // 1-3: a channel message in `data`; more: SysEx
        std::array<std::uint8_t, 3> data; // the channel message
        std::uint32_t offset;            // the SysEx (F0 .. F7) in bytes()
    };
    struct Timed {
        std::uint64_t frame; // after the play's start: the tempo map's time, to the nearest frame
        std::uint32_t tick;
        Message message;
    };
    // "play mid from <tick>": the chase (every event before `tick` but note-ons, sent at the
    // play), then the file's events from `tick` on in mciseq's order, each at its own frame; at
    // `end` (the file's last tick) comes the notify and the port closes, so that tick's own events
    // never sound (as on Windows) and are not in `events`.
    struct Play {
        std::vector<Message> chase;
        std::vector<Timed> events;
        std::uint64_t end = 0;      // frames after the play's start
        std::uint32_t end_tick = 0; // the song position there
    };
    [[nodiscard]] Play schedule(std::uint32_t tick) const;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::uint32_t division() const noexcept { return division_; }
    [[nodiscard]] std::uint32_t length() const noexcept { return end_tick_; } // ticks to the end
    [[nodiscard]] double seconds() const; // the end's time on the tempo map
    // The tick's time on the tempo map in seconds, accumulated in double event by event (an
    // independent check of the schedule's integer frames, used by the tests).
    [[nodiscard]] double tempo_map_seconds(std::uint32_t tick) const;

  private:
    enum class Kind : std::uint8_t { midi, sysex, tempo, end };
    struct Event {
        std::uint32_t tick;
        Kind kind;
        Message message; // midi and sysex
        std::uint32_t tempo; // microseconds per quarter
    };
    std::vector<std::uint8_t> bytes_; // the file
    std::vector<Event> events_;       // in mciseq's order
    std::uint32_t division_ = 0, end_tick_ = 0;
};

} // namespace gswavetable
