// SPDX-License-Identifier: MIT
#include "gswavetable/player.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace gswavetable {
namespace {

// The synth's render cycles: 221 and 220 frames, 10 ms of 22 050 Hz (the WASAPI period that
// calls Render on Windows); cycle k starts here.
STime boundary(std::uint64_t k) {
    return STime((441 * k + 1) / 2);
}

} // namespace

struct Player::Impl {
    std::shared_ptr<const Bank> bank;
    std::unique_ptr<Synth> synth; // the open port
    std::uint64_t cycle = 0;      // the next render cycle
    std::vector<std::int16_t> fifo;
    std::size_t fifo_at = 0;
    std::optional<Sequencer> song;
    Sequencer::Play play;
    std::size_t next = 0;           // the next event of `play` to hand to the synth
    STime origin = 0;               // the synth's frame of the play's frame 0 (negative after a resume)
    STime rendered = 0;             // the synth's frame after the last one in `fifo`
    std::uint64_t paused_frame = 0; // the play's frame where a pause stopped the output
    bool paused = false, ended = false;
    std::uint32_t from = 0;                 // the tick the play started from
    std::uint32_t tick = 0;                 // "status mid position", in ticks
    std::set<std::pair<int, int>> held;     // the notes mciseq has on (SetBit)

    void send(const Sequencer::Message &m, STime time) {
        if (m.size <= 3) {
            synth->play(time, std::span(m.data.data(), m.size));
            const auto type = m.data[0] & 0xF0U;
            const auto key = std::pair{int(m.data[0] & 0x0FU), int(m.data[1])};
            if (type == 0x90 && m.data[2])
                held.insert(key);
            else if (type == 0x80 || type == 0x90)
                held.erase(key);
        } else {
            synth->play(time, song->bytes().subspan(m.offset, m.size));
        }
    }
    // _Stop's AllNotesOff: per channel CC64 0, then a note-off for every note on.
    void all_notes_off(STime time) {
        for (int channel = 0; channel < 16; ++channel) {
            synth->play(time, std::array<std::uint8_t, 3>{std::uint8_t(0xB0 | channel), 0x40, 0x00});
            for (auto it = held.lower_bound({channel, 0}); it != held.end() && it->first == channel;)
                synth->play(time, std::array<std::uint8_t, 3>{std::uint8_t(0x80 | channel), std::uint8_t(it->second), 0x40}),
                    it = held.erase(it);
        }
    }

    // One render cycle. Every event whose frame falls in the cycle (its end included: the engine's
    // QueueNotes takes what is due by the end of the Mix) goes to the synth first, stamped with
    // its own frame, so its note starts there within the render; controllers act at the engine's
    // control spans, as for any stamped event on Windows. The cycle in which the file ends is
    // rendered whole, as the render period runs, but only its frames before the end are kept:
    // there the port closes.
    void render_cycle() {
        const STime start = boundary(cycle), stop = boundary(cycle + 1);
        auto keep = std::uint32_t(stop - start);
        if (song && !paused && !ended) {
            const STime end = origin + STime(play.end);
            if (end <= start) { // the end: the notify, and the port closes
                tick = play.end_tick;
                ended = true;
                synth.reset();
                return;
            }
            while (next < play.events.size() && origin + STime(play.events[next].frame) <= stop) {
                const auto &e = play.events[next++];
                send(e.message, origin + STime(e.frame));
                tick = e.tick;
            }
            if (end < stop)
                keep = std::uint32_t(end - start);
        }
        const auto at = fifo.size();
        fifo.resize(at + 2 * std::size_t(stop - start));
        synth->render(fifo.data() + at, std::uint32_t(stop - start), start);
        fifo.resize(at + 2 * std::size_t(keep));
        rendered = start + keep;
        ++cycle;
    }
};

Player::Player(std::shared_ptr<const Bank> bank) : impl_(std::make_unique<Impl>()) {
    impl_->bank = std::move(bank);
}

Player::~Player() = default;

void Player::play(Sequencer song, std::uint32_t tick) {
    auto &d = *impl_;
    if (!d.bank)
        return;
    d.synth = std::make_unique<Synth>(d.bank); // the port opens: a fresh synth, its clock at 0
    d.cycle = 0;
    d.fifo.clear();
    d.fifo_at = 0;
    d.rendered = 0;
    d.song = std::move(song);
    d.from = d.tick = tick;
    d.play = d.song->schedule(d.tick);
    d.next = 0;
    d.origin = 0;
    d.paused = d.ended = false;
    d.held.clear();
    for (const auto &m : d.play.chase) // sent at the play, before its first events
        d.send(m, 0);
}

void Player::stop() {
    auto &d = *impl_;
    d.synth.reset();
    d.song.reset();
    d.fifo.clear();
    d.fifo_at = 0;
    d.paused = d.ended = false;
    d.held.clear();
}

void Player::pause() {
    auto &d = *impl_;
    if (!d.synth || d.paused || d.ended)
        return;
    // mciseq's pause stops the play (_Stop's AllNotesOff) and closes the MIDI port at once: on
    // Server 2022 the synth rendered no block after the "pause mid" call, so the music and its
    // release tails end there (seen in recordings of the Windows synth). The output stops at the
    // last frame handed out; what the synth rendered past it is dropped, and the events from there
    // on go to the resumed synth again.
    const STime stopped = d.rendered - STime((d.fifo.size() - d.fifo_at) / 2);
    d.all_notes_off(stopped);
    d.synth.reset();
    d.fifo.clear();
    d.fifo_at = 0;
    d.paused = true;
    d.paused_frame = std::uint64_t(stopped - d.origin);
    while (d.next > 0 && d.play.events[d.next - 1].frame >= d.paused_frame)
        --d.next;
    d.tick = d.next > 0 ? d.play.events[d.next - 1].tick : d.from;
}

void Player::resume() {
    auto &d = *impl_;
    if (!d.paused)
        return;
    d.paused = false;
    if (!d.song || !d.bank)
        return;
    // A port opened again is a fresh synth (a play after a pause opens one, as every play does);
    // it gets the chase to where the play waited, every event before the next event's tick but
    // note-ons, as a play from a position does, and the schedule goes on from the paused frame,
    // every event at its own frame after it. (mciseq itself cannot resume: "resume mid" fails
    // with MCIERR_UNSUPPORTED_FUNCTION, and its "play" after a pause sends no chase.)
    d.synth = std::make_unique<Synth>(d.bank);
    d.origin = -STime(d.paused_frame);
    d.cycle = 0;
    d.rendered = 0;
    d.fifo.clear();
    d.fifo_at = 0;
    const auto from = d.next < d.play.events.size() ? d.play.events[d.next].tick : d.play.end_tick;
    for (const auto &m : d.song->schedule(from).chase)
        d.send(m, 0);
}

bool Player::playing() const noexcept {
    return impl_->synth != nullptr;
}

bool Player::ended() const noexcept {
    return impl_->ended;
}

std::uint32_t Player::position() const noexcept {
    return impl_->song ? impl_->tick : 0;
}

std::uint32_t Player::render(std::int16_t *stereo, std::uint32_t frames) {
    auto &d = *impl_;
    std::uint32_t done = 0;
    while (done < frames) {
        if (d.fifo_at == d.fifo.size()) {
            d.fifo.clear();
            d.fifo_at = 0;
            if (!d.synth || (d.render_cycle(), d.fifo.empty())) {
                std::fill(stereo + 2 * std::size_t(done), stereo + 2 * std::size_t(frames), std::int16_t(0));
                return done;
            }
        }
        const auto n = std::min<std::size_t>((d.fifo.size() - d.fifo_at) / 2, frames - done);
        std::copy_n(d.fifo.data() + d.fifo_at, 2 * n, stereo + 2 * std::size_t(done));
        d.fifo_at += 2 * n;
        done += std::uint32_t(n);
    }
    return done;
}

} // namespace gswavetable
