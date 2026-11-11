// SPDX-License-Identifier: MIT
// The sequencer on Standard MIDI Files built here: format 0 and 1, the tempo map to the nearest
// frame, the merge order (tick, track, file order), the note-off before a note-on for a note that
// is on, the chase of a play from a tick, the last tick's events, and files it must reject.
#include "gswavetable/sequencer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace gs = gswavetable;
using Bytes = std::vector<std::uint8_t>;

int failures = 0;

void expect(bool ok, const std::string &what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void put_vlq(Bytes &out, std::uint32_t value) {
    Bytes bytes{std::uint8_t(value & 0x7fU)};
    while ((value >>= 7) != 0)
        bytes.insert(bytes.begin(), std::uint8_t(0x80U | (value & 0x7fU)));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// One track: events at absolute ticks, in order.
struct Track {
    Bytes data;
    std::uint32_t last = 0;
    Track &at(std::uint32_t tick, std::initializer_list<std::uint8_t> event) {
        put_vlq(data, tick - last);
        last = tick;
        data.insert(data.end(), event);
        return *this;
    }
    Track &tempo(std::uint32_t tick, std::uint32_t us) {
        return at(tick, {0xff, 0x51, 0x03, std::uint8_t(us >> 16), std::uint8_t(us >> 8), std::uint8_t(us)});
    }
    Track &sysex(std::uint32_t tick, const Bytes &body) { // body: after F0, ending in F7
        put_vlq(data, tick - last);
        last = tick;
        data.push_back(0xf0);
        put_vlq(data, std::uint32_t(body.size()));
        data.insert(data.end(), body.begin(), body.end());
        return *this;
    }
    Track &end(std::uint32_t tick) { return at(tick, {0xff, 0x2f, 0x00}); }
};

Bytes smf(std::uint16_t format, std::uint16_t division, const std::vector<Track> &tracks) {
    Bytes out{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, std::uint8_t(format), 0, std::uint8_t(tracks.size()),
              std::uint8_t(division >> 8), std::uint8_t(division)};
    for (const auto &t : tracks) {
        const auto n = std::uint32_t(t.data.size());
        out.insert(out.end(), {'M', 'T', 'r', 'k', std::uint8_t(n >> 24), std::uint8_t(n >> 16), std::uint8_t(n >> 8),
                               std::uint8_t(n)});
        out.insert(out.end(), t.data.begin(), t.data.end());
    }
    return out;
}

Bytes bytes_of(const gs::Sequencer &song, const gs::Sequencer::Message &m) {
    if (m.size <= 3)
        return Bytes(m.data.begin(), m.data.begin() + std::ptrdiff_t(m.size));
    const auto s = song.bytes().subspan(m.offset, m.size);
    return Bytes(s.begin(), s.end());
}

std::string hex(const Bytes &b) {
    std::string s;
    char buffer[4];
    for (const auto x : b) {
        std::snprintf(buffer, sizeof buffer, "%02X ", x);
        s += buffer;
    }
    return s;
}

// The frame of a tick at 500 000 us per quarter: the time rounded to the microsecond (these ticks
// never fall on a half), then the nearest frame, a half up.
std::uint64_t frame_at_120_bpm(std::uint32_t tick, std::uint32_t division) {
    const std::uint64_t us = (std::uint64_t(tick) * 1000000 + division) / (2 * std::uint64_t(division));
    return (us * 22050 + 500000) / 1000000;
}

struct Expected {
    std::uint64_t frame;
    std::uint32_t tick;
    Bytes message;
};

void expect_events(const gs::Sequencer &song, const gs::Sequencer::Play &play, const std::vector<Expected> &want,
                   const std::string &name) {
    expect(play.events.size() == want.size(),
           name + ": " + std::to_string(play.events.size()) + " events, expected " + std::to_string(want.size()));
    for (std::size_t i = 0; i < std::min(play.events.size(), want.size()); ++i) {
        const auto &e = play.events[i];
        const auto got = bytes_of(song, e.message);
        expect(e.frame == want[i].frame && e.tick == want[i].tick && got == want[i].message,
               name + ": event " + std::to_string(i) + " is " + hex(got) + "at tick " + std::to_string(e.tick) +
                   ", frame " + std::to_string(e.frame) + "; expected " + hex(want[i].message) + "at tick " +
                   std::to_string(want[i].tick) + ", frame " + std::to_string(want[i].frame));
    }
}

void expect_messages(const gs::Sequencer &song, const std::vector<gs::Sequencer::Message> &got,
                     const std::vector<Bytes> &want, const std::string &name) {
    expect(got.size() == want.size(),
           name + ": " + std::to_string(got.size()) + " messages, expected " + std::to_string(want.size()));
    for (std::size_t i = 0; i < std::min(got.size(), want.size()); ++i)
        expect(bytes_of(song, got[i]) == want[i],
               name + ": message " + std::to_string(i) + " is " + hex(bytes_of(song, got[i])) + "; expected " + hex(want[i]));
}

void format0_default_tempo() {
    Track t;
    t.at(0, {0xc0, 0x05}).at(0, {0xb0, 0x07, 0x64}).at(480, {0x90, 0x3c, 0x64}).at(960, {0x80, 0x3c, 0x40}).end(1920);
    const gs::Sequencer song(smf(0, 480, {t}));
    expect(song.division() == 480 && song.length() == 1920, "format 0: division and length");
    expect(song.seconds() == 2.0, "format 0: 1920 ticks at 120 bpm are 2 s");
    const auto play = song.schedule(0);
    expect(play.chase.empty(), "format 0: a play from 0 has no chase");
    // 120 bpm until a tempo event: 480 ticks are 0.5 s, 11 025 frames.
    expect_events(song, play,
                  {{0, 0, {0xc0, 0x05}}, {0, 0, {0xb0, 0x07, 0x64}}, {11025, 480, {0x90, 0x3c, 0x64}},
                   {22050, 960, {0x80, 0x3c, 0x40}}},
                  "format 0");
    expect(play.end == 44100 && play.end_tick == 1920, "format 0: the end at frame 44 100, tick 1920");
}

void tempo_map_frames() {
    // 96 ticks per quarter at 600 000 us, then 333 333 us: every event's time in microseconds,
    // rounded, then the nearest frame.
    Track t;
    t.tempo(0, 600000)
        .at(1, {0x90, 0x3c, 0x64})   // 6 250 us: 137.8125 frames -> 138
        .at(7, {0x80, 0x3c, 0x00})   // 43 750 us: 964.6875 -> 965
        .tempo(96, 333333)
        .at(97, {0x90, 0x3e, 0x64})  // 603 472.22 us -> 603 472 us: 13 306.56 -> 13 307
        .at(500, {0x80, 0x3e, 0x00}) // 2 002 776.38 us -> 2 002 776 us: 44 161.22 -> 44 161
        .end(501);
    const gs::Sequencer song(smf(0, 96, {t}));
    const auto play = song.schedule(0);
    expect_events(song, play,
                  {{138, 1, {0x90, 0x3c, 0x64}}, {965, 7, {0x80, 0x3c, 0x00}}, {13307, 97, {0x90, 0x3e, 0x64}},
                   {44161, 500, {0x80, 0x3e, 0x00}}},
                  "tempo map");
    for (const auto &e : play.events) {
        const double exact = song.tempo_map_seconds(e.tick) * 22050.0;
        expect(std::abs(double(e.frame) - exact) <= 0.511,
               "tempo map: tick " + std::to_string(e.tick) + " at frame " + std::to_string(e.frame) + ", exactly " +
                   std::to_string(exact));
    }

    // A time exactly half way between two frames goes to the later one: 10 000 us is 220.5 frames.
    Track half;
    half.tempo(0, 10000).at(1, {0x90, 0x3c, 0x64}).end(2);
    const gs::Sequencer tie(smf(0, 1, {half}));
    expect_events(tie, tie.schedule(0), {{221, 1, {0x90, 0x3c, 0x64}}}, "frame tie");
}

void format1_merge_order() {
    // At one tick the first track in the file goes first, and each track keeps its own order.
    Track t0, t1, t2;
    t0.tempo(0, 500000).at(10, {0xb0, 0x0a, 0x00}).end(100);
    t1.at(10, {0x90, 0x40, 0x64}).at(10, {0x90, 0x41, 0x64}).at(20, {0x80, 0x40, 0x00}).end(90);
    t2.at(5, {0xc1, 0x01}).at(10, {0xb1, 0x0b, 0x7f}).end(100);
    const gs::Sequencer song(smf(1, 96, {t0, t1, t2}));
    expect(song.length() == 100, "format 1: the length is the longest track's");
    const auto f = [](std::uint32_t tick) { return frame_at_120_bpm(tick, 96); };
    expect_events(song, song.schedule(0),
                  {{f(5), 5, {0xc1, 0x01}},
                   {f(10), 10, {0xb0, 0x0a, 0x00}},
                   {f(10), 10, {0x90, 0x40, 0x64}},
                   {f(10), 10, {0x90, 0x41, 0x64}},
                   {f(10), 10, {0xb1, 0x0b, 0x7f}},
                   {f(20), 20, {0x80, 0x40, 0x00}}},
                  "format 1");
}

void repeated_note_on() {
    // A note-on for a note that is on gets a note-off with its data first; a note-on with velocity
    // 0 is a note-off. Running status throughout the track.
    Track t;
    t.at(0, {0x90, 0x3c, 0x64}).at(10, {0x3c, 0x50}).at(20, {0x3c, 0x00}).at(30, {0x3c, 0x64}).at(40, {0x80, 0x3c, 0x00});
    t.end(50);
    const gs::Sequencer song(smf(0, 96, {t}));
    const auto f = [](std::uint32_t tick) { return frame_at_120_bpm(tick, 96); };
    expect_events(song, song.schedule(0),
                  {{0, 0, {0x90, 0x3c, 0x64}},
                   {f(10), 10, {0x80, 0x3c, 0x50}},
                   {f(10), 10, {0x90, 0x3c, 0x50}},
                   {f(20), 20, {0x90, 0x3c, 0x00}},
                   {f(30), 30, {0x90, 0x3c, 0x64}},
                   {f(40), 40, {0x80, 0x3c, 0x00}}},
                  "repeated note-on");
}

void chase() {
    const Bytes gs_reset{0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7};
    Track t;
    t.sysex(0, gs_reset)
        .at(0, {0xc0, 0x10})
        .at(0, {0xb0, 0x07, 0x50})
        .at(0, {0x90, 0x3c, 0x64})
        .at(10, {0xe0, 0x00, 0x50})
        .at(15, {0x90, 0x3e, 0x64})
        .at(20, {0x80, 0x3e, 0x00})
        .tempo(24, 250000)
        .at(25, {0x90, 0x3c, 0x00})
        .at(30, {0x90, 0x40, 0x64})
        .at(40, {0x80, 0x40, 0x00})
        .end(96);
    const gs::Sequencer song(smf(0, 96, {t}));
    // From tick 30: everything before it but the note-ons, in order, sent at the play; the play's
    // frame 0 is tick 30, at 250 000 us per quarter from tick 24.
    const auto play = song.schedule(30);
    Bytes reset{0xf0};
    reset.insert(reset.end(), gs_reset.begin(), gs_reset.end());
    expect_messages(song, play.chase,
                    {reset, {0xc0, 0x10}, {0xb0, 0x07, 0x50}, {0xe0, 0x00, 0x50}, {0x80, 0x3e, 0x00}, {0x90, 0x3c, 0x00}},
                    "chase");
    // tick 30: 24 * 5 208.33 + 6 * 2 604.17 = 140 625 us; tick 40: 166 666.67 -> 166 667 us; the
    // event 26 042 us after the play's start: 574.23 frames -> 574.
    expect_events(song, play, {{0, 30, {0x90, 0x40, 0x64}}, {574, 40, {0x80, 0x40, 0x00}}}, "play from 30");
    expect(play.end_tick == 96, "play from 30: the end at tick 96");

    // From beyond the end: the whole file as the chase, no events, the end at once.
    const auto beyond = song.schedule(200);
    expect(beyond.chase.size() == 7 && beyond.events.empty() && beyond.end == 0 && beyond.end_tick == 200,
           "play from beyond the end: " + std::to_string(beyond.chase.size()) + " chased, " +
               std::to_string(beyond.events.size()) + " events, end " + std::to_string(beyond.end));
}

void last_tick() {
    // The last tick's events come with the end: the port closes before they sound.
    Track t0, t1;
    t0.at(0, {0x90, 0x3c, 0x64}).at(96, {0x80, 0x3c, 0x00}).at(96, {0x90, 0x3e, 0x64}).end(96);
    t1.at(48, {0x91, 0x40, 0x64}).end(80);
    const gs::Sequencer song(smf(1, 96, {t0, t1}));
    const auto play = song.schedule(0);
    // 48 ticks are 250 000 us, 5 512.5 frames: a tie, to the later frame.
    expect_events(song, play, {{0, 0, {0x90, 0x3c, 0x64}}, {5513, 48, {0x91, 0x40, 0x64}}}, "last tick");
    expect(play.end == 11025 && play.end_tick == 96, "last tick: the end at frame 11 025, tick 96");
}

void rejected() {
    Track t;
    t.at(0, {0x90, 0x3c, 0x64}).end(10);
    const auto good = smf(0, 96, {t});
    const auto with = [&good](std::size_t at, std::uint8_t value) {
        auto b = good;
        b[at] = value;
        return b;
    };
    const std::vector<std::pair<std::string, Bytes>> files = {
        {"empty", {}},
        {"shorter than a header", Bytes(good.begin(), good.begin() + 13)},
        {"not MThd", with(0, 'R')},
        {"a header of 5 bytes", with(7, 5)},
        {"a header past the end", with(7, 0xff)},
        {"SMPTE time", with(12, 0xe7)},
        {"division 0", with(13, 0)},
    };
    for (const auto &[name, bytes] : files) {
        bool threw = false;
        try {
            static_cast<void>(gs::Sequencer(bytes));
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        expect(threw, "rejected: " + name + " was accepted");
    }
}

} // namespace

int main() {
    format0_default_tempo();
    tempo_map_frames();
    format1_merge_order();
    repeated_note_on();
    chase();
    last_tick();
    rejected();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("sequencer: passed\n");
    return 0;
}
