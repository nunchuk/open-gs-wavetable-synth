// SPDX-License-Identifier: MIT
// The 22 050 -> 48 000 Hz converter (resampler.hpp) against tones: the pass band, the roll-off from
// 8 kHz and the images above 11 kHz of Windows' converter. Levels with tolerances, not hashes: the
// output is float and its filter is computed with libm.
#include "gswavetable/resampler.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

using gswavetable::Resampler;

int failures = 0;

// The stream through the converter in 256-frame blocks, as a mixer feeds it.
std::vector<float> convert(const std::vector<std::int16_t> &stereo) {
    Resampler r;
    std::vector<float> out;
    constexpr std::size_t block = 256;
    std::vector<float> chunk(2 * 512);
    for (std::size_t at = 0; at < stereo.size() / 2; at += block) {
        const auto n = std::min(block, stereo.size() / 2 - at);
        r.push(stereo.data() + 2 * at, n);
        while (const auto got = r.pull(chunk.data(), 512))
            out.insert(out.end(), chunk.begin(), chunk.begin() + std::ptrdiff_t(2 * got));
    }
    return out;
}

// The level of a tone at `hz` in the left channel (Hann-windowed DFT), dB re amplitude 0.5.
double tone_db(const std::vector<float> &stereo, double hz) {
    constexpr double pi = std::numbers::pi;
    const std::size_t skip = 4800, n = stereo.size() / 2 - 2 * skip;
    std::complex<double> sum = 0;
    double weight = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * pi * double(i) / double(n - 1));
        sum += w * double(stereo[2 * (skip + i)]) * std::polar(1.0, -2 * pi * hz * double(skip + i) / 48000.0);
        weight += w;
    }
    return 20 * std::log10(2 * std::abs(sum) / weight / 0.5 + 1e-15);
}

} // namespace

int main() {
    // Windows' converter (the identified filter; recordings of music agree in band levels and
    // images): the pass band, the roll-off and the images at 22 050 - f.
    struct Tone {
        double hz, gain_low, gain_high, image_max;
    };
    const Tone tones[] = {{1000, -0.16, 0.04, -70}, {8000, -0.6, -0.1, -40}, {8500, -1.3, -0.7, -50},
                          {9750, -5.8, -4.8, -45}, {10250, -9.0, -7.9, -27}, {10900, -15.0, -13.8, -15}};
    for (const auto &t : tones) {
        std::vector<std::int16_t> pcm(2 * 2 * 22050);
        for (std::size_t i = 0; i < pcm.size() / 2; ++i)
            pcm[2 * i] = pcm[2 * i + 1] =
                std::int16_t(std::lround(16383.5 * std::sin(2 * std::numbers::pi * t.hz * double(i) / 22050)));
        const auto out = convert(pcm);
        const double gain = tone_db(out, t.hz), image = tone_db(out, 22050 - t.hz);
        std::printf("%.0f Hz at %.2f dB, its image at %.0f Hz at %.1f dB\n", t.hz, gain, 22050 - t.hz, image);
        if (gain < t.gain_low || gain > t.gain_high) {
            std::printf("FAIL: %.0f Hz passes at %.2f dB, Windows' converter at %.2f..%.2f\n", t.hz, gain, t.gain_low,
                        t.gain_high);
            ++failures;
        }
        if (image > t.image_max) {
            std::printf("FAIL: the image of %.0f Hz is at %.1f dB, Windows' at most %.0f\n", t.hz, image, t.image_max);
            ++failures;
        }
    }
    // 2 s in, 2 * 48 000 frames out but the filter's last taps.
    const auto out = convert(std::vector<std::int16_t>(2 * 2 * 22050, 0));
    if (out.size() / 2 + 20 < 96000 || out.size() / 2 > 96000) {
        std::printf("FAIL: %zu frames out of 44 100 in\n", out.size() / 2);
        ++failures;
    }
    if (failures)
        return 1;
    std::printf("resampler: passed\n");
    return 0;
}
