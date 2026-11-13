// SPDX-License-Identifier: MIT
// gswavetable-render [--from TICK] [--48k [transparent|windows]] BANK.dls INPUT.mid OUTPUT.wav
// Renders a MIDI file as Windows plays it through MCI on the Microsoft GS Wavetable synth
// (player.hpp): "play mid from TICK" (default 0) to the file's end, where the port closes.
// Writes 16-bit stereo PCM at 22 050 Hz, the synth's own output; with --48k, 32-bit float stereo
// at 48 000 Hz (resampler.hpp): transparent (the default), the synth's own sound, or windows,
// Windows 10's audio engine's converter, as a 48 kHz endpoint gets it there.
#include "gswavetable/engine.hpp"
#include "gswavetable/player.hpp"
#include "gswavetable/resampler.hpp"
#include "gswavetable/sequencer.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char *usage =
    "usage: gswavetable-render [--from TICK] [--48k [transparent|windows]] BANK.dls INPUT.mid OUTPUT.wav\n";

std::vector<std::uint8_t> read(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(input), {}};
}

// A RIFF WAVE file of stereo samples: format 1 (PCM) or 3 (IEEE float, with the fact chunk).
void write_wav(const std::string &path, std::uint16_t format, std::uint32_t rate, std::uint16_t bits, const void *data,
               std::size_t frames) {
    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&out](std::uint32_t value) { out.write(reinterpret_cast<const char *>(&value), 4); };
    const auto put16 = [&out](std::uint16_t value) { out.write(reinterpret_cast<const char *>(&value), 2); };
    const bool pcm = format == 1;
    const auto block = std::uint16_t(2 * bits / 8);
    const auto bytes = std::uint32_t(frames * block);
    out.write("RIFF", 4);
    put32((pcm ? 36 : 50) + bytes);
    out.write("WAVEfmt ", 8);
    put32(pcm ? 16 : 18);
    put16(format);
    put16(2);
    put32(rate);
    put32(rate * block);
    put16(block);
    put16(bits);
    if (!pcm) {
        put16(0); // cbSize
        out.write("fact", 4);
        put32(4);
        put32(std::uint32_t(frames));
    }
    out.write("data", 4);
    put32(bytes);
    out.write(static_cast<const char *>(data), std::streamsize(bytes));
    if (!out)
        throw std::runtime_error("cannot write " + path);
}

// The 22 050 Hz frames at 48 000 Hz. The output after the end is silence: `ahead` frames of it
// (the converter's look-ahead) complete the last output frames.
template <class Converter> std::vector<float> to_48k(const std::vector<std::int16_t> &pcm, std::size_t ahead) {
    Converter converter;
    const std::size_t frames = pcm.size() / 2;
    converter.push(pcm.data(), frames);
    const std::vector<std::int16_t> silence(2 * ahead, 0);
    converter.push(silence.data(), ahead);
    std::vector<float> out(2 * ((frames * Converter::up + Converter::down - 1) / Converter::down));
    out.resize(2 * converter.pull(out.data(), out.size() / 2));
    return out;
}

} // namespace

int main(int argc, char **argv) {
    namespace gs = gswavetable;
    std::uint32_t from = 0;
    std::string resampler; // empty: 22 050 Hz
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--from") {
            char *end = nullptr;
            const auto value = i + 1 < argc ? std::strtoull(argv[++i], &end, 10) : 0;
            if (!end || *argv[i] == '\0' || *argv[i] == '-' || *end != '\0' || value > UINT32_MAX) {
                std::cerr << "--from takes a tick (a whole number)\n" << usage;
                return 2;
            }
            from = std::uint32_t(value);
        } else if (arg == "--48k") {
            resampler = "transparent";
            if (i + 1 < argc && (std::string(argv[i + 1]) == "transparent" || std::string(argv[i + 1]) == "windows"))
                resampler = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << usage;
            return 0;
        } else if (arg.starts_with("--")) {
            std::cerr << "unknown option " << arg << '\n' << usage;
            return 2;
        } else {
            paths.push_back(arg);
        }
    }
    if (paths.size() != 3) {
        std::cerr << usage;
        return 2;
    }
    try {
        const auto bank = gs::load_dls(read(paths[0]));
        const gs::Sequencer song(read(paths[1]));
        gs::Player player(bank);
        player.play(song, from);
        constexpr std::uint32_t chunk = 256;
        std::vector<std::int16_t> pcm, block(2 * chunk);
        while (player.playing()) { // to the end, where the port closes
            const auto n = player.render(block.data(), chunk);
            pcm.insert(pcm.end(), block.begin(), block.begin() + 2 * std::ptrdiff_t(n));
        }
        const std::size_t frames = pcm.size() / 2;
        if (!resampler.empty()) {
            const auto out = resampler == "windows" ? to_48k<gs::WindowsResampler>(pcm, 6)
                                                    : to_48k<gs::TransparentResampler>(pcm, gs::TransparentResampler::half);
            write_wav(paths[2], 3, 48000, 32, out.data(), out.size() / 2);
            std::cerr << paths[1] << ": " << song.length() << " ticks, " << song.seconds() << " s, " << out.size() / 2
                      << " frames at 48 000 Hz (" << resampler << ") written\n";
        } else {
            write_wav(paths[2], 1, gs::engine_rate, 16, pcm.data(), frames);
            std::cerr << paths[1] << ": " << song.length() << " ticks, " << song.seconds() << " s, " << frames
                      << " frames at 22 050 Hz written\n";
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
