// SPDX-License-Identifier: MIT
// Renders against their goldens: every row of DATA_DIR/golden.tsv is a MIDI file rendered from a
// tick as gswavetable-render renders it, and the SHA-256 of the WAV file the CLI would write. So a
// golden can be checked by hand: gswavetable-render --from TICK BANK MIDI out.wav; shasum -a 256.
//   golden_test DATA_DIR        the rows on tests/data/sine.dls (no other file needed)
//   golden_test DATA_DIR --gm   the rows on gm.dls, from $GSWAVETABLE_DLS; exit 77 (skipped) when
//                               it is not set, not there or not Windows' gm.dls
#include "gswavetable/engine.hpp"
#include "gswavetable/player.hpp"
#include "gswavetable/sequencer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace gs = gswavetable;
using Bytes = std::vector<std::uint8_t>;

// The gm.dls the engine was verified with: C:\Windows\System32\drivers\gm.dls of Windows Server
// 2022, 3 440 660 bytes.
constexpr const char *gm_dls_sha256 = "3229b09b9d7d9f3f4793b0d9b34fe6abc75cfa4a2503c0c90f43ff651ba7f2c0";

// FIPS 180-4 SHA-256.
std::string sha256(const Bytes &message) {
    static constexpr std::array<std::uint32_t, 64> k{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    Bytes m = message;
    const std::uint64_t bits = std::uint64_t(message.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56)
        m.push_back(0);
    for (int i = 7; i >= 0; --i)
        m.push_back(std::uint8_t(bits >> (8 * i)));
    for (std::size_t block = 0; block < m.size(); block += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i)
            w[i] = std::uint32_t(m[block + 4 * i]) << 24 | std::uint32_t(m[block + 4 * i + 1]) << 16 |
                   std::uint32_t(m[block + 4 * i + 2]) << 8 | std::uint32_t(m[block + 4 * i + 3]);
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = h;
        for (std::size_t i = 0; i < 64; ++i) {
            const auto s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const auto ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const auto t1 = v[7] + s1 + ch + k[i] + w[i];
            const auto s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const auto maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const auto t2 = s0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (std::size_t i = 0; i < 8; ++i)
            h[i] += v[i];
    }
    std::string hex;
    char digits[9];
    for (const auto word : h) {
        std::snprintf(digits, sizeof digits, "%08x", word);
        hex += digits;
    }
    return hex;
}

bool read(const std::string &path, Bytes &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

void put(Bytes &out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out.push_back(std::uint8_t(value >> (8 * i)));
}

// The WAV file gswavetable-render writes: 16-bit stereo PCM at 22 050 Hz from the play to the end.
Bytes render_wav(const std::shared_ptr<const gs::Bank> &bank, const Bytes &midi, std::uint32_t from,
                 std::size_t &frames) {
    gs::Player player(bank);
    player.play(gs::Sequencer(midi), from);
    std::vector<std::int16_t> pcm, block(2 * 256);
    while (player.playing()) {
        const auto n = player.render(block.data(), 256);
        pcm.insert(pcm.end(), block.begin(), block.begin() + 2 * std::ptrdiff_t(n));
    }
    frames = pcm.size() / 2;
    const auto bytes = std::uint32_t(pcm.size() * 2);
    Bytes wav{'R', 'I', 'F', 'F'};
    put(wav, 36 + bytes, 4);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(wav, 16, 4);
    put(wav, 1, 2);
    put(wav, 2, 2);
    put(wav, gs::engine_rate, 4);
    put(wav, gs::engine_rate * 4, 4);
    put(wav, 4, 2);
    put(wav, 16, 2);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    put(wav, bytes, 4);
    for (const auto sample : pcm)
        put(wav, std::uint16_t(sample), 2);
    return wav;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "--gm")) {
        std::fprintf(stderr, "usage: golden_test DATA_DIR [--gm]\n");
        return 2;
    }
    const std::string data = std::string(argv[1]) + "/";
    const bool gm = argc == 3;

    // The hash itself first, on the FIPS 180-2 examples.
    const std::string abc = "abc", two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    if (sha256({}) != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ||
        sha256(Bytes(abc.begin(), abc.end())) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ||
        sha256(Bytes(two.begin(), two.end())) != "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") {
        std::printf("FAIL: SHA-256 is wrong on its test vectors\n");
        return 1;
    }

    Bytes bank_bytes;
    std::string bank_name = "sine.dls";
    if (gm) {
        bank_name = "gm.dls";
        const char *path = std::getenv("GSWAVETABLE_DLS");
        if (!path || !*path) {
            std::printf("skipped: GSWAVETABLE_DLS is not set (the path of gm.dls)\n");
            return 77;
        }
        if (!read(path, bank_bytes)) {
            std::printf("skipped: cannot read %s\n", path);
            return 77;
        }
        if (const auto hash = sha256(bank_bytes); hash != gm_dls_sha256) {
            std::printf("skipped: %s is not Windows' gm.dls (SHA-256 %s, expected %s)\n", path, hash.c_str(), gm_dls_sha256);
            return 77;
        }
    } else if (!read(data + "sine.dls", bank_bytes)) {
        std::printf("FAIL: cannot read %ssine.dls\n", data.c_str());
        return 1;
    }
    const auto bank = gs::load_dls(bank_bytes);

    std::ifstream table(data + "golden.tsv");
    if (!table) {
        std::printf("FAIL: cannot read %sgolden.tsv\n", data.c_str());
        return 1;
    }
    int rows = 0, failures = 0;
    for (std::string line; std::getline(table, line);) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        std::string name, dls, midi_name, expected;
        std::uint32_t from = 0;
        if (!(fields >> name >> dls >> midi_name >> from >> expected)) {
            std::printf("FAIL: bad golden.tsv line: %s\n", line.c_str());
            ++failures;
            continue;
        }
        if (dls != bank_name)
            continue;
        Bytes midi;
        if (!read(data + midi_name, midi)) {
            std::printf("FAIL: cannot read %s%s\n", data.c_str(), midi_name.c_str());
            ++failures;
            continue;
        }
        std::size_t frames = 0;
        const auto hash = sha256(render_wav(bank, midi, from, frames));
        ++rows;
        const bool ok = hash == expected;
        failures += ok ? 0 : 1;
        std::printf("%s %s: %s from tick %u, %zu frames, %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), midi_name.c_str(),
                    from, frames, hash.c_str(), ok ? "" : ", expected ", ok ? "" : expected.c_str());
    }
    if (rows == 0) {
        std::printf("FAIL: golden.tsv has no row for %s\n", bank_name.c_str());
        return 1;
    }
    if (failures) {
        std::printf("%d of %d renders differ from their goldens\n", failures, rows);
        return 1;
    }
    std::printf("golden renders on %s: %d passed\n", bank_name.c_str(), rows);
    return 0;
}
