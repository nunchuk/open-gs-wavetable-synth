// Renders a MIDI file to a 22 050 Hz WAV the way Windows' Microsoft GS Wavetable Synth plays it.
//   render_wav gm.dls song.mid song.wav
#include <gswavetable/player.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

static std::vector<std::uint8_t> read(const char *path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    // Throws std::runtime_error for a bank the synth rejects, std::invalid_argument for a bad MIDI file.
    gswavetable::Player player(gswavetable::load_dls(read(argv[1])));
    player.play(gswavetable::Sequencer(read(argv[2]))); // from tick 0; play(song, tick) starts later
    std::vector<std::int16_t> pcm, block(2 * 1024);      // 22 050 Hz, 16-bit, stereo interleaved
    while (player.playing()) {                           // until the file's end
        const auto frames = player.render(block.data(), 1024);
        pcm.insert(pcm.end(), block.begin(), block.begin() + 2 * std::ptrdiff_t(frames));
    }
    // A 44-byte WAV header (PCM, stereo, 22 050 Hz, 16-bit), then the samples (little-endian hosts).
    const auto bytes = std::uint32_t(pcm.size() * 2);
    const std::uint32_t header[] = {0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16,    0x00020001,
                                    22050,      22050 * 4,  0x00100004, 0x61746164, bytes};
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char *>(header), sizeof header);
    out.write(reinterpret_cast<const char *>(pcm.data()), std::streamsize(bytes));
    return out ? 0 : 1;
}
