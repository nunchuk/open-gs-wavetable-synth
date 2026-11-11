// SPDX-License-Identifier: MIT
// The DirectMusic synth core of wdmaud.drv (x86, 10.0.20348.2849), see engine.hpp. Names follow
// the classes and members of the engine's public symbols; the comments give the byte offsets of
// the fields in those classes and the RVAs of the functions (image base 0x64b00000), so each
// rule can be checked against the disassembly.
#include "gswavetable/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace gswavetable {
namespace {

constexpr STime stime_max = std::numeric_limits<STime>::max();

std::int16_t saturate16(std::int32_t value) {
    return std::int16_t(std::clamp<std::int32_t>(value, -32768, 32767));
}

// pmulhw: the high half of a signed 16 x 16 product.
std::int16_t mulhi(std::int16_t a, std::int16_t b) {
    return std::int16_t((std::int32_t(a) * std::int32_t(b)) >> 16);
}

Tables make_tables() {
    Tables t{};
    // CDigitalAudio::Init (RVA 0x109a9): pow(pow(10, c / 100), 0.5) * 4095 for c = -1000..0,
    // pow(2, c / 1200) * 4096 for c = -100..100, pow(2, s / 12) * 4096 for s = -48..48.
    for (int c = -1000; c <= 0; ++c)
        t.vfract[std::size_t(c + 1000)] = std::int32_t(std::pow(std::pow(10.0, double(c) / 100.0), 0.5) * 4095.0);
    for (int c = -100; c <= 100; ++c)
        t.pitch_fine[std::size_t(c + 100)] = std::int32_t(std::pow(2.0, double(c) / 1200.0) * 4096.0);
    for (int s = -48; s <= 48; ++s)
        t.pitch_semi[std::size_t(s + 48)] = std::int32_t(std::pow(2.0, double(s) / 12.0) * 4096.0);
    // CVoiceEG::Init (RVA 0x2b579): log10((i / 200)^2) * 10000 / 96 + 1000.
    t.attack[0] = 0;
    for (int i = 1; i <= 200; ++i) {
        const double x = double(i) / 200.0;
        t.attack[std::size_t(i)] = std::int16_t(std::log10(x * x) * 10000.0 / 96.0 + 1000.0);
    }
    // CVoiceLFO::Init (RVA 0x2b5e1): sin(6.283185307 * i * 0.00390625) * 100.
    for (int i = 0; i < 256; ++i)
        t.sine[std::size_t(i)] = std::int16_t(std::sin(6.283185307 * double(i) * 0.00390625) * 100.0);
    // CVoice::Init (RVA 0x2b4f9): log10(i / 127) * 1000 for i = 1..127, -2500 at 0.
    t.pan[0] = -2500;
    for (int i = 1; i < 128; ++i)
        t.pan[std::size_t(i)] = std::int32_t(std::log10(double(i) / 127.0) * 1000.0);
    return t;
}

// 0x64b02370: MIDI 0..127 to 1/100 dB, 40 log10(v / 127) (velocity, CC7, CC11, master volume).
constexpr std::array<std::int32_t, 128> midi_to_vrel{
    -9600, -8415, -7211, -6506, -6006, -5619, -5302, -5034, -4802, -4598, -4415, -4249, -4098, -3959, -3830,
    -3710, -3598, -3493, -3394, -3300, -3211, -3126, -3045, -2968, -2894, -2823, -2755, -2689, -2626, -2565,
    -2506, -2449, -2394, -2341, -2289, -2238, -2190, -2142, -2096, -2050, -2006, -1964, -1922, -1881, -1841,
    -1802, -1764, -1726, -1690, -1654, -1619, -1584, -1551, -1518, -1485, -1453, -1422, -1391, -1361, -1331,
    -1302, -1273, -1245, -1217, -1190, -1163, -1137, -1110, -1085, -1059, -1034, -1010, -985,  -961,  -938,
    -914,  -891,  -869,  -846,  -824,  -802,  -781,  -759,  -738,  -718,  -697,  -677,  -657,  -637,  -617,
    -598,  -579,  -560,  -541,  -522,  -504,  -486,  -468,  -450,  -432,  -415,  -397,  -380,  -363,  -347,
    -330,  -313,  -297,  -281,  -265,  -249,  -233,  -218,  -202,  -187,  -172,  -157,  -142,  -127,  -113,
    -98,   -84,   -69,   -55,   -41,   -27,   -13,   0};
// 0x64b02170: MIDI 0..127 to 1/100 dB, 20 log10(v / 127) (the reverb and chorus sends, which are
// not mixed: kept as the binary has it).
[[maybe_unused]] constexpr std::array<std::int32_t, 128> midi_to_send{
    -9600, -4207, -3605, -3253, -3003, -2809, -2651, -2517, -2401, -2299, -2207, -2124, -2049, -1979, -1915,
    -1855, -1799, -1746, -1697, -1650, -1605, -1563, -1522, -1484, -1447, -1411, -1377, -1344, -1313, -1282,
    -1253, -1224, -1197, -1170, -1144, -1119, -1095, -1071, -1048, -1025, -1003, -982,  -961,  -940,  -920,
    -901,  -882,  -863,  -845,  -827,  -809,  -792,  -775,  -759,  -742,  -726,  -711,  -695,  -680,  -665,
    -651,  -636,  -622,  -608,  -595,  -581,  -568,  -555,  -542,  -529,  -517,  -505,  -492,  -480,  -469,
    -457,  -445,  -434,  -423,  -412,  -401,  -390,  -379,  -369,  -359,  -348,  -338,  -328,  -318,  -308,
    -299,  -289,  -280,  -270,  -261,  -252,  -243,  -234,  -225,  -216,  -207,  -198,  -190,  -181,  -173,
    -165,  -156,  -148,  -140,  -132,  -124,  -116,  -109,  -101,  -93,   -86,   -78,   -71,   -63,   -56,
    -49,   -42,   -34,   -27,   -20,   -13,   -6,    0};

// CControlLogic::GMReset's part order (0x64b07020): part p plays MIDI channel order[p].
constexpr std::array<std::uint8_t, 16> part_channel{9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15};

} // namespace

const Tables &tables() {
    static const Tables t = make_tables();
    return t;
}

std::int32_t prel_to_pfract(std::int32_t cents) { // RVA 0x2c37f
    const auto &t = tables();
    if (cents > 100) {
        cents = std::min(cents, 4800);
        const std::int32_t whole = cents / 100;
        return std::int32_t((std::uint32_t(t.pitch_fine[std::size_t(cents % 100 + 100)]) << (whole / 12)) *
                            std::uint32_t(t.pitch_semi[std::size_t(whole % 12 + 48)])) >>
               12;
    }
    if (cents < -100) {
        cents = std::max(cents, -4800);
        const std::int32_t whole = -cents / 100; // positive
        return ((t.pitch_fine[std::size_t(-(-cents % 100) + 100)] >> (-cents / 1200)) *
                t.pitch_semi[std::size_t(-(whole % 12) + 48)]) >>
               12;
    }
    return t.pitch_fine[std::size_t(cents + 100)];
}

std::int32_t vrel_to_vfract(std::int32_t vrel) { // RVA 0x2d35c
    std::int32_t index = vrel / 10;
    if (index < -1000)
        index = -1000;
    else if (index > -1)
        index = 0;
    return tables().vfract[std::size_t(index + 1000)];
}

// ---------------------------------------------------------------------------------------------
// Downloaded instrument data (CInstManager, CInstrument, CSourceRegion, CSourceArticulation,
// CSourceSample, CWave).

struct SourceEG { // CSourceEG, the 0x38 bytes CVoiceEG::StartVoice copies
    STime attack = 0;               // +0x00
    STime decay = 0;                // +0x08
    STime release = 0;              // +0x10
    std::int16_t vel_attack = 0;    // +0x18 velocity to attack time, cents
    std::int16_t key_decay = 0;     // +0x1a key to decay (and hold) time, cents
    std::int16_t sustain = 1000;    // +0x1c permille
    std::int16_t pitch_scale = 0;   // +0x1e EG2 to pitch, cents
    STime delay = 0;                // +0x20
    STime hold = 0;                 // +0x28
    std::int16_t key_hold = 0;      // +0x30
    std::int16_t cutoff_scale = 0;  // +0x32
};

struct SourceLFO {                  // CSourceLFO, the 0x28 bytes CVoiceLFO::StartVoice copies
    std::int32_t frequency = 0xedc; // +0x00 phase step, 2^24 per cycle
    STime delay = 0;                // +0x08
    std::int16_t mw_volume = 0;     // +0x10 CC1 to attenuation
    std::int16_t mw_pitch = 0;      // +0x12 CC1 to pitch
    std::int16_t volume = 0;        // +0x14
    std::int16_t pitch = 0;         // +0x16
    std::int16_t pressure_pitch = 0;  // +0x18
    std::int16_t pressure_volume = 0; // +0x1a
    std::int16_t cutoff = 0;          // +0x1c
    std::int16_t mw_cutoff = 0;       // +0x1e
    std::int16_t pressure_cutoff = 0; // +0x20
};

struct Articulation {  // CSourceArticulation
    SourceEG pitch_eg; // +0x00 (EG2)
    SourceEG volume_eg; // +0x38 (EG1)
    SourceLFO lfo;     // +0x70
    std::uint32_t rate = engine_rate; // +0x98
    std::int16_t pan = 0;              // +0x9e
    SourceLFO lfo2;                    // +0xa0, the DLS2 vibrato LFO (never set by DLS1)
};

struct Wave {                       // CWave
    std::vector<std::int16_t> data; // the download buffer, two zero samples past the wave
    std::uint32_t length = 0;       // +0x04 samples + 1
    std::uint32_t rate = 0;         // +0x08
    std::uint32_t id = 0;           // +0x18 download id
    std::uint8_t format = 1;        // +0x20: 1 = 16-bit, 2 = 8-bit (converted to signed)
};

struct Sample {                     // CSourceSample (region + 4), 0x24 bytes copied to CDigitalAudio
    const Wave *wave = nullptr;     // +0x00
    std::uint32_t loop_start = 0;   // +0x04
    std::uint32_t loop_end = 1;     // +0x08
    std::uint32_t length = 0;       // +0x0c
    std::uint32_t rate = engine_rate; // +0x10
    std::int16_t fine_tune = 0;     // +0x14
    std::uint32_t wave_id = 0;      // +0x18
    std::uint8_t format = 0;        // +0x1c
    std::uint8_t one_shot = 1;      // +0x1d
    std::uint8_t unity = 60;        // +0x1e
};

struct Region {                      // CSourceRegion
    Sample sample;                   // +0x04
    std::uint32_t loop_type = 0;     // +0x24
    const Articulation *art = nullptr; // +0x28
    std::int32_t attenuation = 0;    // +0x2c, 1/100 dB
    std::uint8_t non_exclusive = 0;  // +0x30
    std::uint8_t key_high = 127;     // +0x31
    std::uint8_t key_low = 0;        // +0x32
    std::uint8_t key_group = 0;      // +0x33
    std::uint8_t vel_high = 127;     // +0x34
    std::uint8_t vel_low = 0;        // +0x35
    std::uint16_t wave_options = 0;  // +0x36
    std::uint32_t channel = 0;       // +0x38
};

struct Instrument {                    // CInstrument
    std::uint32_t locale = 0;          // +0x08: bank MSB << 16 | LSB << 8 | program | drum << 31
    std::vector<const Region *> regions; // +0x04, list order (the reverse of the file's)

    // CInstrument::ScanForRegion (RVA 0x29f70): the next region after `after` that covers the key
    // and the velocity.
    const Region *scan(std::uint32_t key, std::uint32_t velocity, const Region *after) const {
        std::size_t i = 0;
        if (after)
            i = std::size_t(std::find(regions.begin(), regions.end(), after) - regions.begin()) + 1;
        for (; i < regions.size(); ++i) {
            const auto *r = regions[i];
            if (key >= r->key_low && key <= r->key_high && velocity >= r->vel_low && velocity <= r->vel_high)
                return r;
        }
        return nullptr;
    }
};

struct Bank {
    std::vector<std::unique_ptr<Wave>> waves;
    std::vector<std::unique_ptr<Articulation>> articulations;
    std::vector<std::unique_ptr<Region>> regions;
    std::vector<std::unique_ptr<Instrument>> instruments;
    std::array<std::vector<const Instrument *>, 31> buckets; // CInstManager +0x00, locale % 31

    // CInstManager::GetInstrument (RVA 0x29d89): the first instrument of the locale with a region
    // for the key and the velocity.
    const Instrument *find(std::uint32_t locale, std::uint32_t key, std::uint32_t velocity) const {
        for (const auto *instrument : buckets[locale % 31])
            if (instrument->locale == locale && instrument->scan(key, velocity, nullptr))
                return instrument;
        return nullptr;
    }
};

namespace {

// ---- the DLS file as CDLSCollection reads it

struct Reader {
    std::span<const std::uint8_t> data;
    std::uint32_t u32(std::size_t at) const {
        if (at + 4 > data.size())
            throw std::runtime_error("DLS: truncated chunk");
        return std::uint32_t(data[at]) | std::uint32_t(data[at + 1]) << 8 | std::uint32_t(data[at + 2]) << 16 |
               std::uint32_t(data[at + 3]) << 24;
    }
    std::uint16_t u16(std::size_t at) const {
        if (at + 2 > data.size())
            throw std::runtime_error("DLS: truncated chunk");
        return std::uint16_t(data[at] | data[at + 1] << 8);
    }
};

constexpr std::uint32_t fourcc(const char (&s)[5]) {
    return std::uint32_t(std::uint8_t(s[0])) | std::uint32_t(std::uint8_t(s[1])) << 8 |
           std::uint32_t(std::uint8_t(s[2])) << 16 | std::uint32_t(std::uint8_t(s[3])) << 24;
}

struct Chunk {
    std::uint32_t id, list_type; // list_type for RIFF and LIST
    std::size_t data, size;      // the data after the id and size (and after the list type)
};

std::vector<Chunk> chunks(const Reader &r, std::size_t begin, std::size_t end) {
    std::vector<Chunk> out;
    for (std::size_t p = begin; p + 8 <= end;) {
        const auto id = r.u32(p);
        const auto size = r.u32(p + 4);
        Chunk c{id, 0, p + 8, size};
        if (id == fourcc("RIFF") || id == fourcc("LIST")) {
            c.list_type = r.u32(p + 8);
            c.data = p + 12;
            c.size = size >= 4 ? size - 4 : 0;
        }
        if (c.data + c.size > end)
            throw std::runtime_error("DLS: chunk runs past its parent");
        out.push_back(c);
        p += 8 + std::size_t(size) + (size & 1U);
    }
    return out;
}

struct Connection {
    std::uint16_t source, control, destination, transform;
    std::int32_t scale;
};

struct FileRegion {                        // CDLSRegion as the DMUS_REGION it is downloaded as
    std::uint16_t key_low = 0, key_high = 0, vel_low = 0, vel_high = 0, options = 0, key_group = 0;
    bool has_wsmp = false;
    std::uint16_t unity = 0;
    std::int16_t fine_tune = 0;
    std::int32_t attenuation = 0;
    std::uint32_t loops = 0, loop_type = 0, loop_start = 0, loop_length = 0;
    std::uint16_t link_options = 0;
    std::uint32_t link_channel = 0, table_index = 0;
    std::vector<Connection> connections;
};

struct FileInstrument {
    std::uint32_t regions = 0, bank = 0, program = 0;
    std::vector<FileRegion> region_list;
    std::vector<Connection> connections;
};

std::vector<Connection> read_art(const Reader &r, const Chunk &list) {
    std::vector<Connection> out;
    for (const auto &c : chunks(r, list.data, list.data + list.size)) {
        if (c.id != fourcc("art1") && c.id != fourcc("art2"))
            continue;
        const auto header = r.u32(c.data);
        const auto count = r.u32(c.data + 4);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto at = c.data + header + 12 * std::size_t(i);
            out.push_back({r.u16(at), r.u16(at + 2), r.u16(at + 4), r.u16(at + 6), std::int32_t(r.u32(at + 8))});
        }
    }
    return out;
}

FileRegion read_region(const Reader &r, const Chunk &list) {
    FileRegion g;
    for (const auto &c : chunks(r, list.data, list.data + list.size)) {
        if (c.id == fourcc("rgnh")) {
            g.key_low = r.u16(c.data);
            g.key_high = r.u16(c.data + 2);
            g.vel_low = r.u16(c.data + 4);
            g.vel_high = r.u16(c.data + 6);
            g.options = r.u16(c.data + 8);
            g.key_group = r.u16(c.data + 10);
        } else if (c.id == fourcc("wlnk")) {
            g.link_options = r.u16(c.data);
            g.link_channel = r.u32(c.data + 4);
            g.table_index = r.u32(c.data + 8);
        } else if (c.id == fourcc("wsmp")) { // CDLSWaveSample::LoadWSMP: the first loop is used
            const auto size = r.u32(c.data);
            g.has_wsmp = true;
            g.unity = r.u16(c.data + 4);
            g.fine_tune = std::int16_t(r.u16(c.data + 6));
            g.attenuation = std::int32_t(r.u32(c.data + 8));
            g.loops = r.u32(c.data + 16);
            if (g.loops) {
                const auto at = c.data + size;
                g.loop_type = r.u32(at + 4);
                g.loop_start = r.u32(at + 8);
                g.loop_length = r.u32(at + 12);
            }
        } else if (c.id == fourcc("LIST") && (c.list_type == fourcc("lart") || c.list_type == fourcc("lar2"))) {
            g.connections = read_art(r, c);
        }
    }
    return g;
}

FileInstrument read_instrument(const Reader &r, const Chunk &list) {
    FileInstrument ins;
    for (const auto &c : chunks(r, list.data, list.data + list.size)) {
        if (c.id == fourcc("insh")) {
            ins.regions = r.u32(c.data);
            ins.bank = r.u32(c.data + 4);
            ins.program = r.u32(c.data + 8);
        } else if (c.id == fourcc("LIST") && c.list_type == fourcc("lrgn")) {
            for (const auto &g : chunks(r, c.data, c.data + c.size))
                if (g.id == fourcc("LIST") && (g.list_type == fourcc("rgn ") || g.list_type == fourcc("rgn2")))
                    ins.region_list.push_back(read_region(r, g));
        } else if (c.id == fourcc("LIST") && (c.list_type == fourcc("lart") || c.list_type == fourcc("lar2"))) {
            ins.connections = read_art(r, c);
        }
    }
    return ins;
}

// ---- CSourceArticulation::Init / Download / Verify (RVAs 0x29de5, 0x2952e, 0x2a3fd)

// TimeCents2Samples (RVA 0x2a1d3).
STime time_cents_to_samples(std::int32_t tc, std::uint32_t rate, std::int32_t max_seconds) {
    if (tc == std::numeric_limits<std::int32_t>::min())
        return 0;
    const auto samples = STime(std::pow(2.0, double(tc) / 78643200.0) * double(rate));
    const std::uint32_t cap = rate * std::uint32_t(max_seconds);
    if (samples < 0 || (samples < 0x100000000LL && std::uint32_t(samples) <= cap))
        return samples;
    return STime(cap);
}

// PitchCents2PitchFract (RVA 0x29f11): 440 Hz * 2^((pc - 6900) / 1200) as a 2^24-per-cycle step.
std::int32_t pitch_cents_to_fract(std::int32_t pc, std::uint32_t rate) {
    const double hz = std::pow(2.0, (double(pc) * 1.52587890625e-05 - 6900.0) / 1200.0) * 7381975040.0;
    return std::int32_t(std::uint32_t(std::int64_t(hz / double(rate))));
}

void init_lfo(SourceLFO &lfo, std::uint32_t rate) { // CSourceLFO::Init (RVA 0x29edc)
    lfo = SourceLFO{};
    lfo.frequency = std::int32_t(0x5000000ULL / rate);
}

template <class T> void clamp_field(T &value, T low, T high) {
    value = std::clamp(value, low, high);
}

void verify_eg(SourceEG &eg) { // CSourceEG::Verify (RVA 0x2a436)
    clamp_field<STime>(eg.attack, 0, 0x1aeaa0);
    clamp_field<STime>(eg.decay, 0, 0x1aeaa0);
    clamp_field<std::int16_t>(eg.sustain, 0, 1000);
    clamp_field<STime>(eg.release, 0, 0x1aeaa0);
    clamp_field<std::int16_t>(eg.pitch_scale, -1200, 1200);
    clamp_field<std::int16_t>(eg.key_decay, -12000, 12000);
    clamp_field<std::int16_t>(eg.vel_attack, -12000, 12000);
    clamp_field<std::int16_t>(eg.key_hold, -12000, 12000);
    clamp_field<std::int16_t>(eg.cutoff_scale, -12800, 12800);
}

void verify_lfo(SourceLFO &lfo) { // CSourceLFO::Verify (RVA 0x2a5cc)
    clamp_field<std::int32_t>(lfo.frequency, 0x40, 0x1db0);
    clamp_field<STime>(lfo.delay, 0, 0x6baa8);
    for (auto *depth : {&lfo.volume, &lfo.mw_volume, &lfo.pitch, &lfo.mw_pitch, &lfo.pressure_pitch,
                        &lfo.pressure_volume})
        clamp_field<std::int16_t>(*depth, -1200, 1200);
    for (auto *depth : {&lfo.cutoff, &lfo.mw_cutoff, &lfo.pressure_cutoff})
        clamp_field<std::int16_t>(*depth, -12800, 12800);
}

std::unique_ptr<Articulation> download_articulation(const std::vector<Connection> &connections, std::uint32_t rate) {
    auto art = std::make_unique<Articulation>();
    art->rate = rate;
    init_lfo(art->lfo, rate);
    init_lfo(art->lfo2, rate);
    const auto high = [](std::int32_t scale) { return std::int16_t(std::uint32_t(scale) >> 16); };
    const auto tenths = [](std::int32_t scale) { return std::int16_t((std::uint32_t(scale) * 10U) >> 16); };
    for (const auto &c : connections) {
        if (c.source == 0) {
            switch (c.destination) {
            case 0x209: art->volume_eg.release = time_cents_to_samples(c.scale, rate, 40); break;
            case 0x004: art->pan = std::int16_t((c.scale >> 12) / 125); break;
            case 0x104: art->lfo.frequency = pitch_cents_to_fract(c.scale, rate); break;
            case 0x105: art->lfo.delay = time_cents_to_samples(c.scale, rate, 10); break;
            case 0x206: art->volume_eg.attack = time_cents_to_samples(c.scale, rate, 40); break;
            case 0x207: art->volume_eg.decay = time_cents_to_samples(c.scale, rate, 40); break;
            case 0x20a: art->volume_eg.sustain = high(c.scale); break;
            case 0x30a: art->pitch_eg.attack = time_cents_to_samples(c.scale, rate, 40); break;
            case 0x30b: art->pitch_eg.decay = time_cents_to_samples(c.scale, rate, 40); break;
            case 0x30d: art->pitch_eg.release = time_cents_to_samples(c.scale, rate, 40); break;
            case 0x30e: art->pitch_eg.sustain = high(c.scale); break;
            default: break;
            }
        } else if (c.source == 1) { // LFO
            if (c.control == 0) {
                if (c.destination == 1)
                    art->lfo.volume = tenths(c.scale);
                else if (c.destination == 3)
                    art->lfo.pitch = high(c.scale);
            } else if (c.control == 0x81) { // CC1
                if (c.destination == 1)
                    art->lfo.mw_volume = tenths(c.scale);
                else if (c.destination == 3)
                    art->lfo.mw_pitch = high(c.scale);
            }
        } else if (c.source == 2) { // key-on velocity
            if (c.destination == 0x206)
                art->volume_eg.vel_attack = high(c.scale);
            else if (c.destination == 0x30a)
                art->pitch_eg.vel_attack = high(c.scale);
        } else if (c.source == 3) { // key number
            if (c.destination == 0x207)
                art->volume_eg.key_decay = high(c.scale);
            else if (c.destination == 0x30b)
                art->pitch_eg.key_decay = high(c.scale);
        } else if (c.source == 5 && c.destination == 3) { // EG2 to pitch
            art->pitch_eg.pitch_scale = high(c.scale);
        }
    }
    verify_lfo(art->lfo);
    verify_eg(art->pitch_eg);
    verify_eg(art->volume_eg);
    verify_lfo(art->lfo2);
    return art;
}

// CSourceSample::Verify (RVA 0x2a6fe).
void verify_sample(Sample &s) {
    if (s.wave) {
        std::uint32_t length = s.length;
        if (s.wave->length < length) {
            s.length = s.wave->length;
            length = s.wave->length;
        }
        std::uint32_t end = s.loop_end;
        if (end == 0) {
            s.loop_end = end = 1;
        } else if (length < end) {
            s.loop_end = end = length;
        }
        std::uint32_t start = s.loop_start;
        if (end < start)
            s.loop_start = start = end;
        if (end - start < 6)
            s.one_shot = 1;
    }
    s.rate = std::clamp<std::uint32_t>(s.rate, 3000, 200000);
    if (s.unity > 127)
        s.unity = 127;
    clamp_field<std::int16_t>(s.fine_tune, -1200, 1200);
}

// CSourceSample::CopyFromWave (RVA 0x293c1): the region's loop start becomes the wave's guard
// sample (the last region to use a wave wins), a one-shot region drops it and zeroes it.
void copy_from_wave(Sample &s, Wave &wave) {
    s.length = wave.length;
    s.rate = wave.rate;
    s.format = wave.format;
    if (!s.one_shot) {
        if (s.length <= s.loop_start)
            s.loop_start = 0;
        wave.data[s.length - 1] = wave.data[s.loop_start];
    } else {
        s.length = wave.length - 1;
        wave.data[s.length] = 0;
    }
    verify_sample(s);
}

} // namespace

std::shared_ptr<const Bank> load_dls(std::span<const std::uint8_t> dls) {
    const Reader r{dls};
    if (dls.size() < 12 || r.u32(0) != fourcc("RIFF") || r.u32(8) != fourcc("DLS "))
        throw std::runtime_error("not a DLS file");
    const std::size_t end = std::min<std::size_t>(dls.size(), 8 + std::size_t(r.u32(4)));
    std::uint32_t declared_instruments = 0;
    std::vector<std::uint32_t> cues;
    std::vector<FileInstrument> instruments;
    struct FileWave {
        std::size_t offset;
        std::uint16_t tag = 0, channels = 0, bits = 0;
        std::uint32_t rate = 0;
        std::size_t data = 0, size = 0;
    };
    std::vector<FileWave> waves;
    std::size_t pool = 0;
    for (const auto &c : chunks(r, 12, end)) {
        if (c.id == fourcc("colh")) {
            declared_instruments = r.u32(c.data);
        } else if (c.id == fourcc("ptbl")) {
            const auto header = r.u32(c.data);
            const auto count = r.u32(c.data + 4);
            for (std::uint32_t i = 0; i < count; ++i)
                cues.push_back(r.u32(c.data + header + 4 * std::size_t(i)));
        } else if (c.id == fourcc("LIST") && c.list_type == fourcc("lins")) {
            for (const auto &i : chunks(r, c.data, c.data + c.size))
                if (i.id == fourcc("LIST") && i.list_type == fourcc("ins "))
                    instruments.push_back(read_instrument(r, i));
        } else if (c.id == fourcc("LIST") && c.list_type == fourcc("wvpl")) {
            pool = c.data;
            for (const auto &w : chunks(r, c.data, c.data + c.size)) {
                if (w.id != fourcc("LIST") || w.list_type != fourcc("wave"))
                    continue;
                FileWave fw{w.data - 12 - pool};
                for (const auto &k : chunks(r, w.data, w.data + w.size)) {
                    if (k.id == fourcc("fmt ")) {
                        fw.tag = r.u16(k.data);
                        fw.channels = r.u16(k.data + 2);
                        fw.rate = r.u32(k.data + 4);
                        fw.bits = r.u16(k.data + 14);
                    } else if (k.id == fourcc("data")) {
                        fw.data = k.data;
                        fw.size = k.size;
                    }
                }
                if (fw.tag != 1)
                    throw std::runtime_error("DLS: a wave is not PCM");
                waves.push_back(fw);
            }
        }
    }
    if (cues.empty() || instruments.empty() || instruments.size() != declared_instruments)
        throw std::runtime_error("DLS: collection header, pool table and instruments disagree");

    auto bank = std::make_shared<Bank>();
    // CDLSCollection::Download: the waves in pool-table order, then the instruments.
    for (std::size_t cue = 0; cue < cues.size(); ++cue) {
        const auto found = std::find_if(waves.begin(), waves.end(), [&](const FileWave &w) { return w.offset == cues[cue]; });
        if (found == waves.end())
            throw std::runtime_error("DLS: a pool-table entry names no wave");
        // The download id is the wave's load order (CDLSWave::Loaddata, sdwDLId from 0).
        const auto id = std::uint32_t(found - waves.begin());
        if (found->channels != 1 || (found->bits != 8 && found->bits != 16))
            throw std::runtime_error("DLS: unsupported wave format");
        auto wave = std::make_unique<Wave>();
        wave->id = id;
        wave->rate = found->rate;
        if (found->bits == 8) {
            wave->format = 2;
            wave->length = std::uint32_t(found->size) + 1;
            wave->data.assign(found->size + 2, 0); // 8-bit samples are kept as 16-bit << 8 here
            for (std::size_t i = 0; i < found->size; ++i)
                wave->data[i] = std::int16_t(std::int8_t(dls[found->data + i] - 0x80) * 256);
        } else {
            wave->format = 1;
            const std::size_t n = found->size / 2;
            wave->length = std::uint32_t(n) + 1;
            wave->data.assign(n + 2, 0);
            for (std::size_t i = 0; i < n; ++i)
                wave->data[i] = std::int16_t(r.u16(found->data + 2 * i));
        }
        bank->waves.push_back(std::move(wave));
    }
    const auto wave_by_id = [&](std::uint32_t id) -> Wave * {
        for (auto &w : bank->waves) // CInstManager's wave hash, id % 31, newest first
            if (w->id == id)
                return w.get();
        return nullptr;
    };
    for (const auto &fi : instruments) {
        if (fi.region_list.size() != fi.regions || fi.region_list.empty())
            throw std::runtime_error("DLS: an instrument's region count disagrees");
        auto ins = std::make_unique<Instrument>();
        ins->locale = (fi.bank & 0x7f7fU) << 8 | (fi.program & 0x7fU) | (fi.bank & 0x80000000U);
        std::vector<Region *> mine;
        for (const auto &fr : fi.region_list) { // CInstManager::DownloadInstrument (RVA 0x29986)
            auto region = std::make_unique<Region>();
            auto *g = region.get();
            g->key_high = std::uint8_t(fr.key_high);
            g->key_low = std::uint8_t(fr.key_low);
            g->vel_high = std::uint8_t(fr.vel_high);
            g->vel_low = std::uint8_t(fr.vel_low);
            if (!g->vel_high && !g->vel_low)
                g->vel_high = 127;
            g->non_exclusive = std::uint8_t(fr.options & 1U);
            g->key_group = std::uint8_t(fr.key_group);
            g->attenuation = std::int16_t((std::uint32_t(fr.attenuation) * 10U) >> 16);
            g->sample.fine_tune = fr.fine_tune;
            g->sample.unity = std::uint8_t(fr.unity);
            if (fr.loops == 0) {
                g->sample.one_shot = 1;
            } else {
                g->sample.loop_start = fr.loop_start;
                g->sample.one_shot = 0;
                g->sample.loop_end = fr.loop_length + fr.loop_start;
                g->loop_type = fr.loop_type;
            }
            g->wave_options = fr.link_options;
            g->channel = fr.link_channel;
            if (g->channel != 1 && !(g->wave_options & 2U))
                throw std::runtime_error("DLS: a region's wave link is not mono");
            g->sample.wave_id = fr.table_index;
            if (!fr.connections.empty()) {
                bank->articulations.push_back(download_articulation(fr.connections, engine_rate));
                g->art = bank->articulations.back().get();
            }
            if (auto *wave = wave_by_id(g->sample.wave_id)) {
                g->sample.wave = wave;
                copy_from_wave(g->sample, *wave);
            }
            mine.push_back(g);
            bank->regions.push_back(std::move(region));
        }
        if (!fi.connections.empty()) {
            bank->articulations.push_back(download_articulation(fi.connections, engine_rate));
            for (auto *g : mine)
                if (!g->art)
                    g->art = bank->articulations.back().get();
        }
        bool complete = true;
        for (auto *g : mine)
            complete = complete && g->art;
        if (!complete)
            throw std::runtime_error("DLS: a region has no articulation");
        for (auto it = mine.rbegin(); it != mine.rend(); ++it) // pushed at the list's head
            ins->regions.push_back(*it);
        // DMUS_INSTRUMENT_GM_INSTRUMENT is set, so the instrument goes to its bucket's tail.
        bank->buckets[ins->locale % 31].push_back(ins.get());
        bank->instruments.push_back(std::move(ins));
    }
    return bank;
}

// ---------------------------------------------------------------------------------------------
// The synth (CSynth, CControlLogic, CVoice, CVoiceEG, CVoiceLFO, CDigitalAudio).

namespace {

struct Recorder {                   // CMIDIRecorder: time-stamped values, oldest first
    struct Item {
        STime time;
        std::int32_t data;
    };
    std::vector<Item> items;
    std::int32_t current = 0;       // +0x10
    std::int32_t range = 200;       // +0x18, CPitchBendIn only: the bend range in cents

    // CMIDIRecorder::RecordMIDI (RVA 0x301c5): after the items at or before `time`; an item equal
    // in time and value to one of them is dropped.
    void record(STime time, std::int32_t data) {
        std::size_t i = 0;
        for (; i < items.size() && items[i].time <= time; ++i)
            if (items[i].time == time && items[i].data == data)
                return;
        items.insert(items.begin() + std::ptrdiff_t(i), Item{time, data});
    }
    // CMIDIRecorder::RecordMIDINote (RVA 0x30285): the same, without the duplicate check.
    void record_note(STime time, std::int32_t data) {
        std::size_t i = 0;
        while (i < items.size() && items[i].time <= time)
            ++i;
        items.insert(items.begin() + std::ptrdiff_t(i), Item{time, data});
    }
    // CMIDIRecorder::GetData (RVA 0x30129): the newest value at or before `time`.
    std::int32_t get(STime time) const {
        std::int32_t value = current;
        for (const auto &item : items) {
            if (item.time > time)
                break;
            value = item.data;
        }
        return value;
    }
    // CMIDIRecorder::ClearMIDI (RVA 0x30070): drops what is older than `time`.
    void clear(STime time) {
        std::size_t i = 0;
        while (i < items.size() && items[i].time < time)
            current = items[i++].data;
        items.erase(items.begin(), items.begin() + std::ptrdiff_t(i));
    }
};

struct Note { // CNote
    STime time;
    std::uint8_t part, key, velocity;
};

struct Synth_;
struct Voice;

struct Part { // one of CControlLogic's 16 parts
    Recorder mod, bend, volume, expression, pan, reverb, chorus, cutoff, pressure;
    std::int32_t sustain = 0;            // +0xca0
    std::uint16_t rpn = 0x3fff;          // +0xce0
    std::uint8_t bank_msb = 0;           // +0xd00
    std::uint8_t bank_lsb = 0;           // +0xd10
    std::uint32_t locale = 0;            // +0xd20 bank and program
    std::uint32_t priority = 0;          // +0xd68
    std::uint16_t data_entry = 0;        // +0xdb0
    std::int32_t fine_tune = 0;          // +0xdd4 cents
    std::array<std::int32_t, 12> scale_tuning{}; // +0xe14
    std::int32_t coarse_tune = 0;        // +0x1114 cents
    std::uint8_t channel = 0;            // +0x1154
    std::uint8_t drum = 0;               // +0x1164
    std::int32_t mono = 0;               // +0x1174
    Part() {
        volume.current = 100;
        expression.current = 127;
        pan.current = 64;
        bend.current = 0x2000;
        reverb.current = 40;
        cutoff.current = 64;
    }
};

struct VoiceEG {                // CVoiceEG
    SourceEG src;               // +0x00
    STime start = 0;            // +0x38
    STime stop = 0;             // +0x40
    bool active = false;        // +0x48

    // CVoiceEG::GetLevel (RVA 0x2acf7): 0..1000 (the volume EG's attack through the attack table).
    std::int32_t level(STime time, STime &next, bool volume) const {
        if (time > stop) {
            const STime d = time - stop;
            if (d >= src.release) {
                next = stime_max;
                return 0;
            }
            STime unused = 0;
            const STime at_stop = level(stop, unused, volume);
            const auto value = std::int32_t((at_stop * (src.release - d)) / src.release);
            const STime quarter = src.release >> 2, half = src.release >> 1;
            if (d < quarter - 100)
                next = quarter - d;
            else if (d < half - 100)
                next = half - d;
            else
                next = src.release - d;
            return value;
        }
        STime d = time - start;
        if (d < src.delay) {
            next = src.delay - d;
            return 0;
        }
        d -= src.delay;
        if (d >= src.attack) {
            d -= src.attack;
            if (d < src.hold) {
                next = src.hold - d;
                return volume ? tables().attack[200] : 1000;
            }
            d -= src.hold;
            if (d >= src.decay) {
                next = 44100;
                return src.sustain;
            }
            const auto value = std::int32_t(1000 - (STime(1000 - src.sustain) * d) / src.decay);
            const STime quarter = src.decay >> 2, half = src.decay >> 1;
            if (d < quarter - 100)
                next = quarter - d;
            else if (d < half - 100)
                next = half - d;
            else
                next = src.decay - d;
            return value;
        }
        STime value = 0;
        if (src.attack != 0)
            value = (d * 1000) / src.attack;
        next = src.attack - d;
        value = std::clamp<STime>(value, 0, 1000);
        if (!volume)
            return std::int32_t(value);
        return tables().attack[std::size_t(value / 5)];
    }

    // CVoiceEG::StartVoice (RVA 0x2cb8c).
    STime start_voice(const SourceEG &source, STime when, std::uint16_t key, std::uint16_t velocity, STime min_time) {
        stop = stime_max;
        start = when;
        active = true;
        src = source;
        STime attack = std::max(src.attack, min_time);
        if (src.release < min_time)
            src.release = min_time;
        attack = (attack * prel_to_pfract(std::int32_t(src.vel_attack) * velocity / 127)) / 0x1000;
        src.attack = attack;
        const STime key_scale = prel_to_pfract(std::int32_t(src.key_decay) * key / 127);
        src.hold = (src.hold * key_scale) / 0x1000;
        src.decay = (((src.decay * key_scale) / 0x1000) * (1000 - src.sustain)) / 1000;
        return src.delay != 0 ? src.delay : attack;
    }

    // CVoiceEG::StopVoice (RVA 0x2d304).
    void stop_voice(STime when) {
        if (active) {
            const auto at = level(when, stop, true); // the next-change output lands on `stop`
            src.release = (src.release * at) / 1000;
        }
        stop = when;
    }
    // CVoiceEG::QuickStopVoice (RVA 0x2c4de): the release shortened to at most rate / 70.
    void quick_stop(STime when, std::uint32_t rate) {
        if (active) {
            const auto at = level(when, stop, true);
            src.release = (src.release * at) / 1000;
            if (STime(rate / 70) < src.release)
                src.release = rate / 70;
        }
        stop = when;
    }
    bool in_release(STime time) const { return active && time > stop; } // RVA 0x2b4cb
    // CVoiceEG::GetPitch (RVA 0x2b389).
    std::int32_t pitch(STime time, STime &next) const {
        if (!active)
            return 0;
        if (src.pitch_scale == 0) {
            next = 44100;
            return 0;
        }
        return level(time, next, false) * src.pitch_scale / 1000;
    }
};

struct VoiceLFO {                 // CVoiceLFO
    SourceLFO src;                // +0x00
    STime start = 0;              // +0x28
    const Recorder *mw = nullptr; // +0x30
    STime step = 0;               // +0x38
    const Recorder *pressure = nullptr; // +0x40
    bool active = false;          // +0x44

    STime start_voice(const SourceLFO &source, STime when, const Recorder *wheel, const Recorder *press) {
        mw = wheel;
        pressure = press;
        active = true;
        src = source;
        start = when;
        if (!src.mw_pitch && !src.mw_volume && !src.pitch && !src.volume)
            step = 44100;
        else
            step = 0x200000 / src.frequency;
        return step;
    }
    std::int32_t level(STime time, STime &next) const { // RVA 0x2afd2
        if (!active)
            return 0;
        const STime elapsed = time - (start + src.delay);
        if (elapsed < 0) {
            next = -elapsed;
            return 0;
        }
        next = step;
        const auto phase = std::uint32_t(STime(src.frequency) * elapsed);
        return tables().sine[(phase >> 16) & 0xffU];
    }
    std::int32_t pitch(STime time, STime &next) const { // RVA 0x2b3dd
        if (!active)
            return 0;
        const std::int32_t depth = pressure->get(time) * src.pressure_pitch / 127 + mw->get(time) * src.mw_pitch / 127 +
                                   src.pitch;
        return level(time, next) * depth / 100;
    }
    std::int32_t volume(STime time, STime &next) const { // RVA 0x2b454
        if (!active)
            return 0;
        const std::int32_t depth = pressure->get(time) * src.pressure_volume / 127 +
                                   mw->get(time) * src.mw_volume / 127 + src.volume;
        return level(time, next) * depth / 100;
    }
};

struct DigitalAudio {               // CDigitalAudio
    Sample sample;                  // +0x00
    const std::int16_t *data = nullptr; // +0x28
    std::uint32_t basic_pitch = 0;  // +0x2c m_pfBasicPitch
    std::int32_t last_pitch = 0;    // +0x30 m_pfLastPitch
    std::int32_t last_cents = 0;    // +0x34 m_prLastPitch
    std::uint32_t position = 0;     // +0x38 m_pfLastSample, 20.12
    std::uint32_t loop_start = 0;   // +0x3c
    std::uint32_t loop_end = 0;     // +0x40
    std::uint32_t length = 0;       // +0x44
    bool one_shot = false;          // +0x74
    std::int64_t played = 0;        // +0x98

    // CDigitalAudio::StartVoice (RVA 0x2c565).
    void start_voice(const Sample &s, std::int32_t cents, std::uint16_t key) {
        last_cents = 0;
        sample = s;
        data = s.wave->data.data();
        one_shot = s.one_shot != 0;
        const auto pfract = prel_to_pfract((std::int32_t(key) - s.unity) * 100 + cents + s.fine_tune);
        basic_pitch = (std::uint32_t(pfract) * s.rate) / engine_rate;
        last_pitch = std::int32_t(basic_pitch);
        if (s.length >= 0x80000)
            throw std::runtime_error("GS synth: a sample of 512 Ki frames or more (big-sample mode) is not implemented");
        if (s.loop_end - s.loop_start > 0x7ffffU)
            one_shot = true;
        loop_start = s.loop_start << 12;
        position = 0;
        loop_end = s.loop_end << 12;
        if (STime(s.loop_end) <= STime(s.loop_start))
            one_shot = true;
        length = s.length << 12;
        played = 0;
    }

    // CDigitalAudio::Mix16X (RVA 0x10adf), the MMX mixer, for spans of more than 8 frames.
    std::uint32_t mix16x(std::int16_t *out, std::uint32_t frames, std::uint32_t period, std::int32_t step_l,
                         std::int32_t step_r, const std::int32_t *last_vf, std::int32_t pitch_step, std::uint32_t end,
                         std::uint32_t loop_length) {
        std::uint32_t pos = position;
        std::uint32_t pitch = std::uint32_t(last_pitch);
        std::uint32_t pitch_acc = pitch << 8;
        std::uint32_t acc_l = std::uint32_t(last_vf[0]) << 8, acc_r = std::uint32_t(last_vf[1]) << 8;
        std::uint32_t vol_l = acc_l >> 5, vol_r = acc_r >> 5;
        std::uint32_t counter = period;
        const std::uint32_t limit = frames * 2;
        std::uint32_t i = 0;
        const auto word = [](std::uint32_t v) { return saturate16(std::int32_t(v)); }; // packssdw
        const auto interpolate = [this](std::uint32_t p) {                              // pmaddwd, psrad 12
            const std::uint32_t frac = p & 0xfffU;
            const std::size_t index = p >> 12;
            const std::int32_t sum = std::int32_t(data[index]) * std::int32_t(4096 - frac) +
                                     std::int32_t(data[index + 1]) * std::int32_t(frac);
            return std::int16_t(sum >> 12);
        };
        const auto add = [&](std::size_t frame, std::int16_t s) { // pmulhw, paddsw
            out[2 * frame] = saturate16(out[2 * frame] + mulhi(word(vol_l), s));
            out[2 * frame + 1] = saturate16(out[2 * frame + 1] + mulhi(word(vol_r), s));
        };
        for (;;) {
            if (i >= limit)
                break;
            if (pos >= end) {
                if (!loop_length)
                    break;
                pos -= loop_length;
            }
            const std::uint32_t a = pos;
            pos = a + pitch;
            counter -= 2;
            if (counter == 0) {
                acc_l += std::uint32_t(step_l);
                acc_r += std::uint32_t(step_r);
                vol_l = (std::int32_t(acc_l) < 0 ? 0U : acc_l) >> 5;
                vol_r = (std::int32_t(acc_r) < 0 ? 0U : acc_r) >> 5;
                pitch_acc += std::uint32_t(pitch_step);
                pitch = pitch_acc >> 8;
                counter = period;
            }
            i += 2;
            bool single = i >= limit;
            if (!single && pos >= end) {
                if (!loop_length)
                    single = true;
                else
                    pos -= loop_length;
            }
            if (single) {
                add(i / 2 - 1, interpolate(a));
                break;
            }
            const std::uint32_t b = pos;
            const auto sa = interpolate(a), sb = interpolate(b);
            pos = b + pitch;
            add(i / 2 - 1, sa);
            add(i / 2, sb);
            i += 2;
        }
        last_pitch = std::int32_t(pitch);
        position = pos;
        return i >> 1;
    }

    // CDigitalAudio::Mix16 (RVA 0x10f01), the C mixer.
    std::uint32_t mix16(std::int16_t *out, std::uint32_t frames, std::uint32_t period, std::int32_t step_l,
                        std::int32_t step_r, const std::int32_t *last_vf, std::int32_t pitch_step, std::int32_t end,
                        std::int32_t loop_length) {
        auto pos = std::int32_t(position);
        std::int32_t pitch = last_pitch;
        std::int32_t pitch_acc = pitch << 8;
        std::int32_t vol_l = last_vf[0], vol_r = last_vf[1];
        std::int32_t acc_l = vol_l << 8, acc_r = vol_r << 8;
        std::uint32_t counter = period;
        std::uint32_t frame = 0;
        for (; frame < frames; ++frame) {
            if (end <= pos) {
                if (!loop_length)
                    break;
                pos -= loop_length;
            }
            if (--counter == 0) {
                pitch_acc += pitch_step;
                acc_l += step_l;
                pitch = pitch_acc >> 8;
                vol_l = acc_l >> 8;
                acc_r += step_r;
                vol_r = acc_r >> 8;
                counter = period;
            }
            const std::int32_t index = pos >> 12;
            const std::int32_t frac = pos & 0xfff;
            pos += pitch;
            const std::int32_t s0 = data[index];
            const std::int32_t s = (((data[index + 1] - s0) * frac) >> 12) + s0;
            out[2 * frame] = saturate16(out[2 * frame] + std::int16_t((vol_l * s) >> 13));
            out[2 * frame + 1] = saturate16(out[2 * frame + 1] + std::int16_t((vol_r * s) >> 13));
        }
        last_pitch = pitch;
        position = std::uint32_t(pos);
        return frame;
    }

    // CDigitalAudio::Mix (RVA 0x2b644) for a stereo buffer without the filter: false when the
    // sample has ended or cannot loop.
    bool mix(std::int16_t *out, std::uint32_t frames, std::uint32_t delta, const std::int32_t *new_vf,
             const std::int32_t *last_vf, std::int32_t cents, bool mmx) {
        if (frames == 0) {
            last_pitch = std::int32_t((std::uint32_t(prel_to_pfract(cents)) * basic_pitch)) >> 12;
            last_cents = cents;
            return true;
        }
        if (!data)
            return false;
        const std::int32_t pitch_delta = std::abs(cents - last_cents) * 2;
        std::uint32_t period = std::max<std::uint32_t>(delta, std::uint32_t(pitch_delta)) >> 1;
        last_cents = cents;
        if (period == 0) {
            period = 0x200;
        } else {
            period = (frames << 3) / period;
            period = std::clamp<std::uint32_t>(period, 1, 0x200);
        }
        period = (period + 3) & ~3U;
        const std::int32_t new_pitch = std::int32_t(std::uint32_t(prel_to_pfract(cents)) * basic_pitch) >> 12;
        const auto muldiv = [](std::int32_t a, std::int32_t b, std::int32_t c) { // MulDiv: rounded
            const std::int64_t p = std::int64_t(a) * b;
            const std::int64_t half = std::abs(std::int64_t(c)) / 2;
            const std::int64_t q = p >= 0 ? (p + half) / c : (p - half) / c;
            return std::int32_t(q);
        };
        const std::int32_t pitch_step = muldiv(new_pitch - last_pitch, std::int32_t(period << 8), std::int32_t(frames));
        const std::int32_t step_l = muldiv(new_vf[0] - last_vf[0], std::int32_t(period << 8), std::int32_t(frames));
        const std::int32_t step_r = muldiv(new_vf[1] - last_vf[1], std::int32_t(period << 8), std::int32_t(frames));
        std::uint32_t left = frames;
        std::int16_t *at = out;
        for (;;) {
            const bool use_mmx = mmx && left > 8;
            std::uint32_t end = 0, loop_length = 0, started = 0;
            if (one_shot) {
                end = length;
                started = position;
            } else {
                end = loop_end;
                loop_length = loop_end - loop_start;
                if (std::int32_t(loop_length) <= new_pitch || std::int32_t(loop_length) > std::int32_t(length))
                    return false;
            }
            const std::uint32_t mixed = use_mmx ? mix16x(at, left, period, step_l, step_r, last_vf, pitch_step, end, loop_length)
                                                : mix16(at, left, period, step_l, step_r, last_vf, pitch_step,
                                                        std::int32_t(end), std::int32_t(loop_length));
            if (one_shot) {
                std::int32_t advanced = std::int32_t(position - started) >> 12;
                if (mixed < left) {
                    played += advanced - 1;
                    return false; // the sample ended (no wave articulation follows it)
                }
                played += advanced;
                break;
            }
            if (mixed >= left)
                break;
            left -= mixed;
            at += 2 * mixed;
            position += loop_start - loop_end;
        }
        last_pitch = new_pitch;
        return true;
    }
};

struct BusIds { // CBusIds: left, right, reverb, chorus
    std::uint32_t count = 4;
    std::array<std::uint32_t, 32> ids{0, 1, 0x40, 0x41};
};

struct Voice {                         // CVoice
    Voice *next = nullptr;             // +0x000
    VoiceLFO lfo;                      // +0x008
    VoiceEG pitch_eg;                  // +0x050
    VoiceEG volume_eg;                 // +0x0a0
    DigitalAudio audio;                // +0x0f0
    const Recorder *bend = nullptr, *expression = nullptr, *volume = nullptr, *pan = nullptr, *reverb = nullptr,
                   *chorus = nullptr; // +0x190..
    STime span = 0;                    // +0x1b0 frames to the next change
    STime last_mix = 0;                // +0x1b8
    std::int32_t art_pan = 0;          // +0x1c0
    std::uint32_t note_id = 0;         // +0x1c8
    STime start = 0;                   // +0x1d0
    STime stop = stime_max;            // +0x1d8
    STime scheduled_stop = 0;          // +0x1e0
    bool in_use = false;               // +0x1e8
    bool note_on = false;              // +0x1ec
    bool tag = false;                  // +0x1f0 quick-stopped
    std::int32_t vr_volume = 0;        // +0x1f4 the volume EG in 1/100 dB
    bool sustained = false;            // +0x1f8
    std::uint16_t part = 0;            // +0x1fc
    std::uint16_t key = 0;             // +0x1fe
    std::uint32_t non_exclusive = 0;   // +0x200
    std::uint32_t key_group = 0;       // +0x204
    std::uint32_t locale = 0;          // +0x208
    std::uint32_t priority = 0;        // +0x20c
    const void *control = nullptr;     // +0x210
    const Region *region = nullptr;    // +0x218
    VoiceLFO lfo2;                     // +0x240
    BusIds buses;                      // +0x288
    std::uint32_t loop_type = 0;       // +0x30c
    std::uint32_t multichannel = 0;    // +0x310
    std::array<std::int32_t, 32> last_volume{}; // +0x314
    std::array<std::int32_t, 32> bus_gain{};    // +0x394
    std::array<std::int32_t, 32> last_vfract{}; // +0x414
};

} // namespace

struct Synth::Impl {
    std::shared_ptr<const Bank> bank;
    std::array<Voice, 40> storage;
    Voice *free_list = nullptr;  // +0x20
    Voice *extra_list = nullptr; // +0x24
    Voice *in_use = nullptr;     // +0x28
    std::int16_t extra_voices = 8; // +0x2e
    STime last = 0;              // +0x18
    STime max_span = (engine_rate + 19) / 20; // +0x98
    std::uint32_t note_counter = 0; // 0x64b347d4
    bool mmx = true;              // m_sfMMXEnabled, +0x260
    Synth::Statistics statistics; // not the synth's: counted for diagnostics
    // CControlLogic
    std::array<Part, 16> parts;
    Recorder notes;               // CNoteIn (+0x08)
    std::int32_t master_volume = 0; // +0xdd0
    std::int32_t xg = 0;          // +0xda8
    std::int32_t gs = 0;          // +0xdac

    explicit Impl(std::shared_ptr<const Bank> b) : bank(std::move(b)) {
        // CSynth::CSynth: 32 voices onto the free list, 8 onto the extra list (both at the head).
        for (std::size_t i = 0; i < 32; ++i) {
            storage[i].next = free_list;
            free_list = &storage[i];
        }
        for (std::size_t i = 32; i < 40; ++i) {
            storage[i].next = extra_list;
            extra_list = &storage[i];
        }
        gm_reset();
        for (auto &p : parts) {
            p.locale = 0;
            p.sustain = 0;
        }
    }

    // CControlLogic::GMReset (RVA 0x2dbbf).
    void gm_reset() {
        for (std::size_t p = 0; p < 16; ++p) {
            auto &part = parts[p];
            part.data_entry = 0;
            part.fine_tune = 0;
            part.drum = 0;
            part.scale_tuning.fill(0);
            part.rpn = 0x3fff;
            part.coarse_tune = 0;
            part.channel = part_channel[p];
            part.mono = 0;
            part.priority = 0x80000010U - std::uint32_t(p);
            part.bank_msb = 0;
            part.bank_lsb = 0;
        }
        parts[0].drum = 1;
        master_volume = 0;
        gs = 0;
        xg = 0;
    }

    // ---- CControlLogic::RecordMIDI (RVA 0x2e6c6)
    void record_midi(STime time, std::uint8_t status, std::uint8_t d1, std::uint8_t d2) {
        const std::uint8_t type = status & 0xf0U;
        const std::uint8_t channel = status & 0x0fU;
        for (std::uint32_t p = 0; p < 16; ++p) {
            auto &part = parts[p];
            if (part.channel != channel)
                continue;
            const std::int32_t code = std::int32_t(p << 16);
            switch (type) {
            case 0x80: notes.record_note(time, code | d1 << 8); break;
            case 0x90: notes.record_note(time, code | d1 << 8 | d2); break;
            case 0xc0: notes.record_note(time, code | 0xf100 | d1); break;
            case 0xd0: part.pressure.record(time, d1); break;
            case 0xe0: part.bend.record(time, d2 << 7 | d1); break;
            case 0xb0:
                switch (d1) {
                case 0: if (gs) notes.record_note(time, code | 0xf200 | d2); break;
                case 1: part.mod.record(time, d2); break;
                case 6: notes.record_note(time, code | 0xfa00 | d2); break;
                case 7: part.volume.record(time, d2); break;
                case 10: part.pan.record(time, d2); break;
                case 11: part.expression.record(time, d2); break;
                case 32: if (gs) notes.record_note(time, code | 0xf300 | d2); break;
                case 38: notes.record_note(time, code | 0xf900 | d2); break;
                case 64: notes.record_note(time, code | 0xfe00 | d2); break;
                case 74: part.cutoff.record(time, d2); break;
                case 91: part.reverb.record(time, d2); break;
                case 93: part.chorus.record(time, d2); break;
                case 98: case 99: notes.record_note(time, code | 0xf800 | d2); break;
                case 100: notes.record_note(time, code | 0xf700 | d2); break;
                case 101: notes.record_note(time, code | 0xf600 | d2); break;
                case 120: notes.record_note(time, code | 0xfd00); break;
                case 121: // reset all controllers: CC7 and CC10 only when the value is not 0
                    if (d2 != 0) {
                        part.volume.record(time, 100);
                        part.pan.record(time, 0x40);
                    }
                    part.expression.record(time, 0x7f);
                    part.bend.record(time, 0x2000);
                    part.mod.record(time, 0);
                    part.pressure.record(time, 0);
                    part.reverb.record(time, 0x28);
                    part.chorus.record(time, 0);
                    part.cutoff.record(time, 0x40);
                    notes.record_note(time, code | 0xfe00);
                    break;
                case 123: notes.record_note(time, code | 0xff00); break;
                case 126:
                    notes.record_note(time, code | 0xf500 | d2);
                    notes.record_note(time, code | 0xfd00);
                    break;
                case 127:
                    notes.record_note(time, code | 0xf400 | d2);
                    notes.record_note(time, code | 0xfd00);
                    break;
                default: break;
                }
                break;
            default: break;
            }
        }
    }

    // ---- CControlLogic::RecordSysEx (RVA 0x2eb86): GS, XG and GM resets, GS part set-up, master
    // volume. `m` starts at F0.
    void record_sysex(STime time, std::span<const std::uint8_t> m) {
        if (m.size() < 6)
            return;
        bool reset = false;
        if (m[1] == 0x41) { // Roland
            if (m.size() < 11 || m[3] != 0x42 || m[4] != 0x12)
                return;
            const std::uint32_t part = m[6] & 0x0fU;
            const std::uint32_t address = (std::uint32_t(m[5]) << 8 | (m[6] & 0xf0U)) << 8 | m[7];
            if (address == 0x40007f) { // GS reset
                gm_reset();
                xg = 0;
                gs = 1;
                reset = true;
            } else if (address == 0x401002) { // part to MIDI channel
                if (gs && m[8] != 0)
                    notes.record_note(time, std::int32_t(part << 16 | 0xfb00U | std::uint8_t(m[8] - 1)));
                return;
            } else if (address == 0x401015) { // use rhythm part
                if (!gs)
                    return;
                parts[part].drum = m[8];
                defaults_after_reset(time);
                return;
            } else if (address == 0x401040) { // scale tuning
                if (gs)
                    for (std::size_t i = 0; i < 12 && 9 + i < m.size() && !(m[9 + i] & 0x80U); ++i)
                        parts[part].scale_tuning[i] = std::int32_t(m[8 + i]) - 0x40;
                return;
            } else {
                return;
            }
        } else if (m[1] == 0x43) { // XG system on
            if (m.size() < 8 || m[3] != 0x4c || m[4] != 0 || m[5] != 0 || m[6] != 0x7e || m[7] != 0)
                return;
            gs = 0;
            gm_reset();
            xg = 1;
            parts[0].drum = 0;
            parts[0].bank_msb = 0x7f;
            reset = true;
        } else if (m[1] == 0x7e) { // GM system on
            if (m[3] != 0x09)
                return;
            gm_reset();
            reset = true;
        } else if (m[1] == 0x7f) { // master volume
            if (m[3] != 0x04 || m[4] != 0x01)
                return;
            notes.record_note(time, std::int32_t(0xfc00U | m[6]));
            return;
        } else {
            return;
        }
        defaults_after_reset(time);
        if (reset) {
            for (std::uint32_t p = 0; p < 16; ++p) {
                const std::int32_t code = std::int32_t(p << 16);
                if (p == 0 && xg)
                    notes.record_note(time - 1, 0xf27f);
                else
                    notes.record_note(time - 1, code | 0xf200);
                notes.record_note(time - 1, code | 0xf300);
                notes.record_note(time, code | 0xf100);
            }
        }
    }

    // RecordSysEx's tail after a reset (and CControlLogic::Flush): every part's sound off,
    // sustain off and controllers at their defaults.
    void defaults_after_reset(STime time) {
        for (auto &part : parts) { // Flush: what is queued after now is dropped
            for (auto *r : {&part.mod, &part.bend, &part.volume, &part.expression, &part.pan, &part.pressure,
                            &part.reverb, &part.chorus, &part.cutoff})
                std::erase_if(r->items, [](const Recorder::Item &i) { return i.time >= 0; });
        }
        for (auto &item : notes.items)
            if (item.time >= 0) {
                item.time = 0;
                item.data &= ~0xffff00;
            }
        for (std::uint32_t p = 0; p < 16; ++p) {
            auto &part = parts[p];
            const std::int32_t code = std::int32_t(p << 16);
            notes.record_note(time, code | 0xfd00);
            notes.record_note(time, code | 0xfe00);
            part.volume.record(time, 100);
            part.pan.record(time, 0x40);
            part.expression.record(time, 0x7f);
            part.bend.record(time, 0x2000);
            part.mod.record(time, 0);
            part.pressure.record(time, 0);
            part.reverb.record(time, 0x28);
            part.chorus.record(time, 0);
            part.cutoff.record(time, 0x40);
        }
    }

    // ---- voices

    // CSynth::QueueVoice (RVA 0x28898): the in-use list is kept in ascending priority.
    void queue_voice(Voice *v) {
        if (!in_use) {
            v->next = nullptr;
            in_use = v;
            return;
        }
        if (v->priority < in_use->priority) {
            v->next = in_use;
            in_use = v;
            return;
        }
        Voice *previous = in_use;
        for (Voice *current = in_use->next; current; previous = current, current = current->next) {
            if (v->priority < current->priority) {
                previous->next = v;
                v->next = current;
                return;
            }
        }
        previous->next = v;
        v->next = nullptr;
    }
    void unlink(Voice *v) {
        if (in_use == v) {
            in_use = v->next;
        } else {
            for (Voice *p = in_use; p; p = p->next)
                if (p->next == v) {
                    p->next = v->next;
                    break;
                }
        }
        v->next = nullptr;
    }

    // CSynth::OldestVoice (RVA 0x285f3).
    Voice *oldest_voice() {
        Voice *best = in_use;
        if (!best)
            return nullptr;
        for (Voice *v = best->next; v; v = v->next) {
            if (v->tag)
                continue;
            if (best->tag) {
                best = v;
            } else if (v->priority <= best->priority) {
                if (v->note_on) {
                    if (best->note_on && v->start < best->start)
                        best = v;
                } else if (best->note_on || v->vr_volume < best->vr_volume) {
                    best = v;
                }
            }
        }
        return best->tag ? nullptr : best;
    }

    // CSynth::StealVoice (RVA 0x28ec8): an in-use voice of at most `priority` is cut now.
    Voice *steal_voice(std::uint32_t priority) {
        Voice *best = nullptr;
        for (Voice *v = in_use; v; v = v->next) {
            if (v->priority > priority)
                continue;
            if (!best) {
                best = v;
            } else if (!v->note_on) {
                if (best->note_on || v->vr_volume < best->vr_volume)
                    best = v;
            } else if (v->start < best->start) {
                best = v;
            }
        }
        if (best) {
            ++statistics.stolen;
            best->in_use = false;
            best->audio = DigitalAudio{}; // CDigitalAudio::ClearVoice drops the wave
            unlink(best);
        }
        return best;
    }

    // CSynth::StealNotes (RVA 0x28e45): keeps 8 voices in reserve, quick-stopping the oldest.
    void steal_notes(STime time) {
        int count = 0;
        for (Voice *v = extra_list; v; v = v->next)
            ++count;
        int need = extra_voices - count;
        if (need <= 0)
            return;
        while (Voice *v = free_list) {
            free_list = v->next;
            --need;
            v->next = extra_list;
            extra_list = v;
            if (need < 1)
                return;
        }
        for (Voice *v = in_use; v; v = v->next)
            if (v->tag)
                --need;
        for (; need > 0; --need) {
            Voice *v = oldest_voice();
            if (!v)
                return;
            ++statistics.reserve_stops;
            quick_stop(v, time);
        }
    }

    // CVoice::StopVoice (RVA 0x2d26d).
    void stop_voice(Voice *v, STime time) {
        if (!v->note_on)
            return;
        if (time <= v->start)
            time = v->start + 1;
        v->pitch_eg.stop_voice(time);
        v->volume_eg.stop_voice(time);
        v->stop = time;
        v->note_on = false;
        v->sustained = false;
        v->scheduled_stop = 0;
        if (v->loop_type == 1)
            v->audio.one_shot = true;
    }
    // CVoice::QuickStopVoice (RVA 0x2c41f).
    void quick_stop(Voice *v, STime time) {
        v->tag = true;
        if (!v->note_on && !v->sustained) {
            v->volume_eg.quick_stop(v->stop, engine_rate);
            return;
        }
        if (time <= v->start)
            time = v->start + 1;
        v->pitch_eg.stop_voice(time);
        v->volume_eg.quick_stop(time, engine_rate);
        v->note_on = false;
        v->sustained = false;
        v->stop = time;
    }

    // CVoice::GetNewPitch (RVA 0x2b097).
    std::int32_t new_pitch(Voice *v, STime time) {
        STime next = v->span;
        std::int32_t pitch = v->lfo.pitch(time, next);
        v->span = std::min(v->span, next);
        pitch += v->lfo2.pitch(time, next);
        v->span = std::min(v->span, next);
        pitch += v->pitch_eg.pitch(time, next);
        v->span = std::min(v->span, next);
        return ((v->bend->get(time) - 0x2000) * v->bend->range >> 13) + pitch;
    }
    // CVoice::GetNewVolume (RVA 0x2b19b): the voice's left and right volume in 1/100 dB.
    void new_volume(Voice *v, STime time, std::int32_t &left, std::int32_t &right) {
        STime next = v->span;
        std::int32_t vol = 0;
        if (v->volume_eg.active)
            vol = v->volume_eg.level(time, next, true) * 0x60 / 10 - 0x2580;
        v->vr_volume = vol;
        v->span = std::min(v->span, next);
        vol += v->lfo.volume(time, next);
        v->span = std::min(v->span, next);
        vol += midi_to_vrel[std::size_t(v->expression->get(time))];
        vol += midi_to_vrel[std::size_t(v->volume->get(time))]; // CSynth +0x8c: the volume is live
        left = right = vol;                                    // CSynth +0x84, the gain, is 0
        if (!v->multichannel) {                                // CSynth +0xa4 stereo, +0x88 live pan
            const std::int32_t pan = v->pan ? std::clamp(v->pan->get(time) + v->art_pan, 0, 127) : 0x3f;
            left = tables().pan[std::size_t(127 - pan)] + vol;
            right = tables().pan[std::size_t(pan)] + vol;
        }
    }

    // CVoice::StartVoice (RVA 0x2c6bd).
    bool start_voice(Voice *v, const Region *region, STime time, Part &part, std::uint16_t key, std::uint16_t velocity,
                     std::int32_t master, std::int32_t cents) {
        const Articulation *art = region->art;
        if (!art)
            return false;
        v->loop_type = region->loop_type;
        const std::int32_t gain = master + midi_to_vrel[velocity] + std::int16_t(region->attenuation);
        v->key_group = region->key_group;
        v->non_exclusive = region->non_exclusive;
        v->art_pan = art->pan;
        v->multichannel = region->wave_options & 2U;
        // The bus loop with Render's buses {0: left, 1: right}: reverb and chorus are not mixed.
        for (std::size_t i = 0; i < 2; ++i) {
            v->bus_gain[i] = gain;
            v->last_volume[i] = -9600;
            v->last_vfract[i] = vrel_to_vfract(-9600);
        }
        v->span = v->lfo.start_voice(art->lfo, time, &part.mod, &part.pressure);
        v->span = std::min(v->span, v->lfo2.start_voice(art->lfo2, time, &part.mod, &part.pressure));
        v->span = std::min(v->span, v->pitch_eg.start_voice(art->pitch_eg, time, key, velocity, 0));
        v->span = std::min(v->span, v->volume_eg.start_voice(art->volume_eg, time, key, velocity, engine_rate / 1000));
        if (v->span > max_span)
            v->span = max_span;
        if (!region->sample.wave)
            return false;
        v->audio.start_voice(region->sample, cents, key);
        v->bend = &part.bend;
        v->expression = &part.expression;
        v->pan = &part.pan;
        v->reverb = &part.reverb;
        v->chorus = &part.chorus;
        v->volume = &part.volume;
        v->note_on = true;
        v->tag = false;
        v->sustained = false;
        v->stop = stime_max;
        v->last_mix = time - 1;
        v->start = time;
        v->scheduled_stop = 0;
        if (v->span == 0) {
            const std::int32_t pitch = new_pitch(v, time);
            v->audio.mix(nullptr, 0, 0, nullptr, nullptr, pitch, mmx);
        }
        v->vr_volume = 0;
        return true;
    }

    // CControlLogic::QueueNotes (RVA 0x2dd1d).
    void queue_notes(STime start, STime end) {
        while (!notes.items.empty() && notes.items.front().time <= end) {
            const auto item = notes.items.front();
            notes.items.erase(notes.items.begin());
            Note note{std::max(item.time, start), std::uint8_t(item.data >> 16), std::uint8_t(item.data >> 8),
                      std::uint8_t(item.data)};
            auto &part = parts[note.part];
            if (note.key < 0x80) {
                if (note.velocity == 0) { // note off
                    std::uint32_t id = 0;
                    for (Voice *v = in_use; v; v = v->next) {
                        if (v->note_on && !v->sustained && v->key == note.key && v->part == note.part &&
                            (id == 0 || id == v->note_id)) {
                            id = v->note_id;
                            if (part.sustain == 0)
                                stop_voice(v, note.time);
                            else
                                v->sustained = true;
                        }
                    }
                    continue;
                }
                note_on(note, part);
                continue;
            }
            const std::uint8_t value = note.velocity;
            switch (note.key) {
            case 0xf1: part.locale = std::uint32_t(part.bank_msb) << 16 | std::uint32_t(part.bank_lsb) << 8 | value; break;
            case 0xf2: part.bank_msb = value; break;
            case 0xf3: part.bank_lsb = value; break;
            case 0xf4: part.mono = 0; break;
            case 0xf5: part.mono = 1; break;
            case 0xf6: part.rpn = std::uint16_t((part.rpn & 0x7fU) + value * 0x80U); break;
            case 0xf7: part.rpn = std::uint16_t((part.rpn & 0x3f80U) + value); break;
            case 0xf8: part.rpn = 0x3fff; break;
            case 0xf9:
                part.data_entry = std::uint16_t((part.data_entry & 0xff80U) | value);
                if (part.rpn == 1)
                    part.fine_tune = (std::int32_t(part.data_entry) - 0x2000) * 100 / 8192;
                break;
            case 0xfa:
                part.data_entry = std::uint16_t(std::uint32_t(value) << 7 | (part.data_entry & 0xc07fU));
                if (part.rpn == 0)
                    part.bend.range = value * 100;
                else if (part.rpn == 1)
                    part.fine_tune = (std::int32_t(part.data_entry) - 0x2000) * 100 / 8192;
                else if (part.rpn == 2)
                    part.coarse_tune = (std::int32_t(value) - 0x40) * 100;
                break;
            case 0xfb: part.channel = value; break;
            case 0xfc: master_volume = midi_to_vrel[value & 0x7fU]; break;
            case 0xfd:
                for (Voice *v = in_use; v; v = v->next)
                    if (v->part == note.part)
                        quick_stop(v, note.time);
                break;
            case 0xfe:
                part.sustain = value;
                if (value == 0)
                    for (Voice *v = in_use; v; v = v->next)
                        if (v->sustained && v->part == note.part)
                            stop_voice(v, note.time);
                break;
            case 0xff:
                for (Voice *v = in_use; v; v = v->next)
                    if (v->note_on && !v->sustained && v->part == note.part) {
                        if (part.sustain == 0)
                            stop_voice(v, note.time);
                        else
                            v->sustained = true;
                    }
                break;
            default: break;
            }
        }
    }

    void note_on(const Note &note, Part &part) {
        std::uint32_t locale = part.locale | (part.drum ? 0x80000000U : 0U);
        if (part.mono)
            for (Voice *v = in_use; v; v = v->next)
                if (v->note_on && v->part == note.part)
                    stop_voice(v, note.time);
        const Instrument *instrument = bank->find(locale, note.key, note.velocity);
        if (!instrument) {
            if (locale & 0x80000000U) {
                locale = 0x80000000U;
                instrument = bank->find(locale, note.key, note.velocity);
            } else if (xg) {
                if ((locale & 0x7f0000U) == 0x7f0000U) {
                    locale &= 0x7f007fU;
                    instrument = bank->find(locale, note.key, note.velocity);
                    if (!instrument) {
                        locale = 0x7f0000U;
                        instrument = bank->find(locale, note.key, note.velocity);
                    }
                } else {
                    locale &= 0x7fU;
                    instrument = bank->find(locale, note.key, note.velocity);
                }
            }
        }
        if (!instrument)
            return;
        const std::uint32_t id = ++note_counter;
        ++statistics.notes;
        for (const Region *region = instrument->scan(note.key, note.velocity, nullptr); region;
             region = instrument->scan(note.key, note.velocity, region)) {
            if (!region->non_exclusive) // the same note again: the old voice fades fast
                for (Voice *v = in_use; v; v = v->next)
                    if (v->part == note.part && v->key == note.key && v->region == region)
                        quick_stop(v, note.time);
            if (region->key_group)
                for (Voice *v = in_use; v; v = v->next)
                    if (v->key_group == region->key_group && v->part == note.part && v->locale == locale)
                        quick_stop(v, note.time);
            Voice *v = free_list;
            if (v) {
                free_list = v->next;
            } else if ((v = extra_list) != nullptr) {
                extra_list = v->next;
            } else {
                v = steal_voice(part.priority);
                if (!v)
                    continue;
            }
            std::int32_t cents = part.scale_tuning[note.key % 12] + part.fine_tune;
            if (!part.drum && (!xg || (locale & 0x7f0000U) != 0x7f0000U))
                cents += part.coarse_tune;
            v->part = note.part;
            v->key = note.key;
            v->locale = locale;
            v->priority = part.priority;
            v->region = region;
            v->note_id = id;
            v->control = this;
            if (start_voice(v, region, note.time, part, note.key, note.velocity, master_volume, cents)) {
                v->in_use = true;
                queue_voice(v);
            } else {
                v->next = free_list;
                free_list = v;
            }
        }
    }

    // CVoice::Mix (RVA 0x2bd22) for a stereo buffer that starts at `start`.
    void mix_voice(Voice *v, std::int16_t *buffer, STime start, STime end) {
        STime start_mix = std::max(v->start, start);
        STime last_mix = v->last_mix;
        if (last_mix >= end || end < 0)
            return;
        if (last_mix < start_mix) {
            last_mix = start_mix;
            if (start_mix >= end) {
                v->last_mix = start;
                return;
            }
        }
        bool going = true, ok = true;
        STime end_mix = start;
        while (true) {
            if (!going) {
                ok = false;
                break;
            }
            end_mix = std::min(v->span + last_mix, end);
            v->span = max_span;
            if (v->last_mix < v->stop && v->stop < end && v->stop - v->last_mix < max_span)
                v->span = v->stop - v->last_mix;
            const std::int32_t pitch = new_pitch(v, end_mix);
            std::int32_t left = 0, right = 0;
            new_volume(v, end_mix, left, right);
            if (v->volume_eg.in_release(end_mix) && v->vr_volume < -8000) {
                v->audio.one_shot = true;
                going = false;
            }
            std::int32_t delta = std::abs(left - v->last_volume[0]);
            if (delta < 0)
                delta = 0;
            v->last_volume[0] = left;
            const std::array<std::int32_t, 2> old{v->last_vfract[0], v->last_vfract[1]};
            const std::array<std::int32_t, 2> target{vrel_to_vfract(left + v->bus_gain[0]),
                                                     vrel_to_vfract(right + v->bus_gain[1])};
            delta = std::max(delta, std::abs(right - v->last_volume[1]));
            v->last_volume[1] = right;
            v->last_vfract[0] = target[0];
            v->last_vfract[1] = target[1];
            ok = v->audio.mix(buffer + 2 * (last_mix - start), std::uint32_t(end_mix - last_mix), std::uint32_t(delta),
                              target.data(), old.data(), pitch, mmx);
            last_mix = end_mix;
            if (last_mix >= end)
                break;
        }
        v->in_use = going && ok;
        if (!v->in_use) {
            v->audio = DigitalAudio{};
            v->stop = end_mix;
        }
        v->last_mix = end_mix;
    }

    // CSynth::Mix (RVA 0x282ab) and CSynth::FinishMix (RVA 0x107c0).
    void render(std::int16_t *stereo, std::uint32_t frames, STime position) {
        std::fill(stereo, stereo + 2 * std::size_t(frames), std::int16_t(0));
        const STime end = position + frames;
        steal_notes(end);
        queue_notes(position, end);
        for (Voice *v = in_use; v;) {
            Voice *next = v->next;
            if (v->scheduled_stop != 0 && v->scheduled_stop < end)
                stop_voice(v, v->scheduled_stop);
            mix_voice(v, stereo, position, end);
            if (!v->in_use) {
                unlink(v);
                v->next = free_list;
                free_list = v;
            }
            v = next;
        }
        for (auto &part : parts)
            for (auto *r : {&part.mod, &part.bend, &part.volume, &part.expression, &part.pan, &part.pressure,
                            &part.reverb, &part.chorus, &part.cutoff})
                r->clear(end);
        const std::uint32_t samples = frames * 2;
        std::uint32_t i = 0;
        if (mmx && samples > 3) // psllw/psrad by 16/15 and packssdw: doubled, saturated
            for (const std::uint32_t m = samples & ~3U; i < m; ++i)
                stereo[i] = saturate16(std::int32_t(stereo[i]) * 2);
        for (; i < samples; ++i) // the C tail clamps to +-0x7fff
            stereo[i] = std::int16_t(std::clamp(std::int32_t(stereo[i]) * 2, -0x7fff, 0x7fff));
        if (last < end)
            last = end;
        std::uint32_t voices = 0;
        for (const Voice *v = in_use; v; v = v->next)
            ++voices;
        statistics.most_voices = std::max(statistics.most_voices, voices);
    }
};

Synth::Synth(std::shared_ptr<const Bank> bank) : impl_(std::make_unique<Impl>(std::move(bank))) {}
Synth::~Synth() = default;

void Synth::play(STime time, std::span<const std::uint8_t> message) {
    if (message.empty())
        return;
    if (message.size() < 5) { // CSynth::PlayBuffer: a DWORD-sized event is a short message
        impl_->record_midi(time, message[0], message.size() > 1 ? message[1] : 0, message.size() > 2 ? message[2] : 0);
        return;
    }
    impl_->record_sysex(time, message);
}

void Synth::render(std::int16_t *stereo, std::uint32_t frames, STime position) {
    impl_->render(stereo, frames, position);
}

bool Synth::voices_active() const noexcept {
    return impl_->in_use != nullptr;
}

STime Synth::last_time() const noexcept {
    return impl_->last;
}

const Synth::Statistics &Synth::statistics() const noexcept {
    return impl_->statistics;
}

} // namespace gswavetable
