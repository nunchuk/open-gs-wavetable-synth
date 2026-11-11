// SPDX-License-Identifier: MIT
#include "gswavetable/sequencer.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace gswavetable {
namespace {

std::uint32_t read_vlq(std::span<const std::uint8_t> data, std::size_t &position) {
    std::uint32_t value = 0;
    while (position < data.size()) {
        const auto byte = data[position++];
        value = (value << 7U) | (byte & 0x7FU);
        if (!(byte & 0x80U))
            break;
    }
    return value;
}

std::uint32_t big_endian(std::span<const std::uint8_t> data, std::size_t at, std::size_t bytes) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i)
        value = value << 8U | data[at + i];
    return value;
}

Sequencer::Message short_message(std::uint8_t status, std::uint8_t d1, std::uint8_t d2, std::uint32_t size) {
    return {size, {status, d1, d2}, 0};
}

} // namespace

Sequencer::Sequencer(std::span<const std::uint8_t> smf) : bytes_(smf.begin(), smf.end()) {
    const std::span<const std::uint8_t> data(bytes_);
    if (data.size() < 14 || big_endian(data, 0, 4) != 0x4D546864U) // MThd
        throw std::invalid_argument("not a Standard MIDI File");
    const auto header = big_endian(data, 4, 4);
    const auto tracks = big_endian(data, 10, 2);
    division_ = big_endian(data, 12, 2);
    if (header < 6 || 8 + std::size_t(header) > data.size() || (division_ & 0x8000U) || division_ == 0)
        throw std::invalid_argument("unsupported MIDI file header");
    struct Ordered {
        Event event;
        std::uint32_t track, order;
    };
    std::vector<Ordered> all;
    std::size_t position = 8 + header;
    for (std::uint32_t track = 0; track < tracks && position + 8 <= data.size(); ++track) {
        if (big_endian(data, position, 4) != 0x4D54726BU) // MTrk
            break;
        const auto length = big_endian(data, position + 4, 4);
        std::size_t p = position + 8;
        const std::size_t end = std::min(data.size(), p + length);
        std::uint32_t tick = 0, order = 0;
        std::uint8_t running = 0;
        bool ended = false;
        while (p < end && !ended) {
            tick += read_vlq(data.first(end), p);
            if (p >= end)
                break;
            const auto byte = data[p];
            Event event{tick, Kind::midi, {}, 0};
            if (byte == 0xFF) {
                if (p + 2 > end)
                    break;
                const auto type = data[p + 1];
                p += 2;
                const auto size = read_vlq(data.first(end), p);
                if (p + size > end)
                    break;
                if (type == 0x51 && size == 3) {
                    event.kind = Kind::tempo;
                    event.tempo = big_endian(data, p, 3);
                    all.push_back({event, track, order++});
                } else if (type == 0x2F) {
                    event.kind = Kind::end;
                    all.push_back({event, track, order++});
                    ended = true;
                }
                p += size;
            } else if (byte == 0xF0 || byte == 0xF7) {
                const auto at = p;
                ++p;
                const auto size = read_vlq(data.first(end), p);
                if (p + size > end)
                    break;
                if (byte == 0xF0) { // F0 and the body (which ends in F7); F7 escapes are not sent
                    event.kind = Kind::sysex;
                    // The message as sent: F0 then the body; the length bytes are skipped.
                    event.message = {size + 1, {}, std::uint32_t(at)};
                    all.push_back({event, track, order++});
                    // Keep F0 adjacent to the body: copy it in front (the VLQ sits in between).
                    bytes_[p - 1] = 0xF0;
                    all.back().event.message.offset = std::uint32_t(p - 1);
                }
                p += size;
            } else {
                if (byte & 0x80U) {
                    running = byte;
                    ++p;
                } else if (!running) {
                    ++p; // malformed: skip the byte
                    continue;
                }
                const bool two = (running & 0xF0U) == 0xC0U || (running & 0xF0U) == 0xD0U;
                const std::uint8_t d1 = p < end ? data[p++] : 0;
                const std::uint8_t d2 = two ? 0 : (p < end ? data[p++] : 0);
                event.message = short_message(running, d1, d2, two ? 2 : 3);
                all.push_back({event, track, order++});
            }
        }
        end_tick_ = std::max(end_tick_, tick);
        position += 8 + std::size_t(length);
    }
    // _GetNextEvent: the track with the smallest pending delta, the first in file order on a tie,
    // keeps the turn while its next delta is 0.
    std::stable_sort(all.begin(), all.end(), [](const Ordered &a, const Ordered &b) {
        if (a.event.tick != b.event.tick)
            return a.event.tick < b.event.tick;
        return a.track != b.track ? a.track < b.track : a.order < b.order;
    });
    for (const auto &o : all)
        events_.push_back(o.event);
}

Sequencer::Play Sequencer::schedule(std::uint32_t from) const {
    Play out;
    // The tempo map, without mciseq's truncated tempo per tick and rounding per delta: every
    // event's time in microseconds, summed event by event in double and rounded to the
    // microsecond (ties to even, as Python's round()), then the frame nearest to it (ties up). A
    // frame is at most 0.511 frames (23 us) from the exact time and the error never accumulates.
    // 120 bpm until the first tempo event (SetUpToPlay).
    double us = 0;
    std::uint64_t tempo = 500000;
    std::uint32_t last = 0;
    const auto advance = [&](std::uint32_t tick) {
        us += double(std::uint64_t(tick - last) * tempo) / double(division_);
        last = tick;
    };
    const auto whole = [](double x) { // Python's round(): to the nearest integer, ties to even
        const double below = std::floor(x), rest = x - below;
        return std::int64_t(below) + (rest > 0.5 || (rest == 0.5 && std::fmod(below, 2.0) != 0.0) ? 1 : 0);
    };
    std::set<std::pair<int, int>> on;
    const auto note = [&on](const Message &m, std::vector<Message> &into) {
        const auto type = m.data[0] & 0xF0U;
        const auto key = std::pair{int(m.data[0] & 0x0FU), int(m.data[1])};
        if (type == 0x90 && m.data[2]) {
            if (on.count(key)) // SendMIDI: a note-off with the same data first
                into.push_back(short_message(std::uint8_t(m.data[0] & 0xEFU), m.data[1], m.data[2], 3));
            on.insert(key);
        } else if (type == 0x80 || type == 0x90) {
            on.erase(key);
        }
        into.push_back(m);
    };
    // The seek (SendAllEventsB4 in mode 1): what comes before `from`, without note-ons.
    std::size_t i = 0;
    for (; i < events_.size() && events_[i].tick < from; ++i) {
        const auto &e = events_[i];
        if (e.kind == Kind::end) // a track's end is no event of the flattened file
            continue;
        advance(e.tick);
        if (e.kind == Kind::tempo) {
            tempo = e.tempo;
        } else if (e.kind == Kind::sysex) {
            out.chase.push_back(e.message);
        } else if (e.kind == Kind::midi) {
            const auto type = e.message.data[0] & 0xF0U;
            if (type == 0x90 && e.message.data[2])
                continue;
            note(e.message, out.chase);
        }
    }
    // The play's frame 0 is the time of `from` (every event before it has an earlier tick); each
    // event comes at the frame nearest to its time after that.
    const auto at = [&](std::uint32_t tick) { return us + double(std::uint64_t(tick - last) * tempo) / double(division_); };
    const auto start = whole(at(from));
    const auto frame = [&](double time) {
        const auto after = std::max<std::int64_t>(0, whole(time) - start);
        return (std::uint64_t(after) * std::uint64_t(engine_rate) + 500000) / 1000000;
    };
    std::vector<Message> messages;
    for (; i < events_.size(); ++i) {
        const auto &e = events_[i];
        if (e.kind == Kind::end)
            continue;
        // The last tick's events come with the end: mciseq sends its AllNotesOff and then them as
        // the play stops, and the port closes before any of it sounds (live captures; the MCI
        // schedule never sent that callback either).
        if (e.tick >= end_tick_)
            break;
        advance(e.tick);
        if (e.kind == Kind::tempo) {
            tempo = e.tempo;
            continue;
        }
        messages.clear();
        if (e.kind == Kind::sysex)
            messages.push_back(e.message);
        else if (e.kind == Kind::midi)
            note(e.message, messages);
        for (const auto &m : messages)
            out.events.push_back({frame(us), e.tick, m});
    }
    // The end of the file (the last track's end), where the notify comes and the port closes; a
    // play from beyond it ends at once.
    out.end = frame(at(std::max(end_tick_, last)));
    out.end_tick = std::max(from, end_tick_);
    return out;
}

double Sequencer::seconds() const {
    return tempo_map_seconds(end_tick_);
}

double Sequencer::tempo_map_seconds(std::uint32_t tick) const {
    double seconds = 0, tempo = 500000;
    std::uint32_t last = 0;
    for (const auto &e : events_) {
        if (e.tick > tick)
            break;
        seconds += double(e.tick - last) * tempo / 1e6 / double(division_);
        last = e.tick;
        if (e.kind == Kind::tempo)
            tempo = e.tempo;
    }
    return seconds + double(tick - last) * tempo / 1e6 / double(division_);
}

} // namespace gswavetable
