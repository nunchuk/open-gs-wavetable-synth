// Plays a MIDI file the way an audio device asks for it: 48 kHz float frames on demand, rendered
// at the synth's 22 050 Hz and converted (transparently) as they are needed. The "device" here is a
// loop that asks for 512 frames at a time and writes them to a raw file (interleaved float32
// stereo); call Music::fill from your audio API's callback instead (Core Audio, WASAPI, ALSA, SDL,
// miniaudio, Web Audio). A Player and its converter belong to one thread at a time: open the bank
// and the song elsewhere, then hand the Music to the audio thread.
//   audio_callback gm.dls song.mid song-48k.f32
#include <gswavetable/player.hpp>
#include <gswavetable/resampler.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace gs = gswavetable;

class Music {
  public:
    Music(std::shared_ptr<const gs::Bank> bank, gs::Sequencer song) : player_(std::move(bank)) {
        player_.play(std::move(song));
    }

    // Writes `frames` stereo frames at 48 kHz; after the song's end, silence. Returns false once
    // the whole song is out.
    bool fill(float *stereo, std::size_t frames) {
        std::size_t done = 0;
        while (done < frames) {
            if (ended_ && pulled_ == total_) {
                std::fill(stereo + 2 * done, stereo + 2 * frames, 0.0F);
                return false;
            }
            auto want = frames - done;
            if (ended_)
                want = std::min(want, std::size_t(total_ - pulled_));
            const auto got = converter_.pull(stereo + 2 * done, want);
            done += got;
            pulled_ += got;
            if (got < want)
                feed();
        }
        return !(ended_ && pulled_ == total_);
    }

  private:
    // The converter needs more 22 050 Hz frames: the next ones from the player or, after the end,
    // the silence that brings out the converter's last frames.
    void feed() {
        if (player_.playing()) {
            std::int16_t block[2 * 256];
            const auto n = player_.render(block, 256);
            converter_.push(block, n);
            rendered_ += n;
        } else if (!ended_) {
            const std::vector<std::int16_t> silence(2 * gs::TransparentResampler::half, 0);
            converter_.push(silence.data(), gs::TransparentResampler::half);
            ended_ = true; // the song lasts rendered_ frames at 22 050 Hz, total_ at 48 kHz
            total_ = (rendered_ * gs::TransparentResampler::up + gs::TransparentResampler::down - 1) /
                     gs::TransparentResampler::down;
        }
    }

    gs::Player player_;
    gs::TransparentResampler converter_;
    std::uint64_t rendered_ = 0, pulled_ = 0, total_ = 0;
    bool ended_ = false;
};

static std::vector<std::uint8_t> read(const char *path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    Music music(gs::load_dls(read(argv[1])), gs::Sequencer(read(argv[2])));
    std::ofstream out(argv[3], std::ios::binary);
    std::vector<float> buffer(2 * 512);
    for (bool more = true; more;) { // the device's callbacks
        more = music.fill(buffer.data(), 512);
        out.write(reinterpret_cast<const char *>(buffer.data()), std::streamsize(buffer.size() * sizeof(float)));
    }
    return out ? 0 : 1;
}
