// SPDX-License-Identifier: MIT
// The 22 050 -> 48 000 Hz converters (resampler.hpp) against tones: the transparent one's flat
// pass band and missing images, and the pass band, roll-off from 8 kHz and images above 11 kHz of
// Windows' converter. Levels with tolerances, not hashes: the output is float and the filters are
// computed with libm.
#include "gswavetable/resampler.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

using gswavetable::TransparentResampler;
using gswavetable::WindowsResampler;

int failures = 0;

// The stream through a converter in 256-frame blocks, as a mixer feeds it.
template <class Converter> std::vector<float> convert(const std::vector<std::int16_t> &stereo) {
    Converter r;
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

std::vector<std::int16_t> tone(double hz) { // 2 s at half scale, both channels, 22 050 Hz
    std::vector<std::int16_t> pcm(2 * 2 * 22050);
    for (std::size_t i = 0; i < pcm.size() / 2; ++i)
        pcm[2 * i] = pcm[2 * i + 1] =
            std::int16_t(std::lround(16383.5 * std::sin(2 * std::numbers::pi * hz * double(i) / 22050)));
    return pcm;
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

// Where an image of a tone at f lands in the 48 kHz output: 22 050 k - f or 22 050 k + f, folded
// into 0 .. 24 000 Hz.
double folded(double hz) {
    hz = std::fmod(std::abs(hz), 48000.0);
    return hz > 24000 ? 48000 - hz : hz;
}

// The transparent converter: a tone up to 10.5 kHz passes within 0.01 dB, a tone at 11 kHz (the
// stop band's edge) is 70 dB down, and every image of a tone up to 11 kHz (22 050 k -+ f for
// k = 1..4, folded) lies at least 90 dB below the tone that went in.
void check_transparent() {
    for (const double hz : {100.0, 1000.0, 5000.0, 8000.0, 10000.0, 10500.0, 10800.0, 11000.0}) {
        const auto out = convert<TransparentResampler>(tone(hz));
        const double gain = tone_db(out, hz);
        double worst = -300, worst_hz = 0;
        for (int k = 1; k <= 4; ++k)
            for (const double image : {22050.0 * k - hz, 22050.0 * k + hz})
                if (const double db = tone_db(out, folded(image)); db > worst)
                    worst = db, worst_hz = folded(image);
        std::printf("transparent: %.0f Hz at %.4f dB, its loudest image at %.0f Hz at %.1f dB\n", hz, gain, worst_hz,
                    worst);
        if (hz <= 10500 && std::abs(gain) > 0.01) {
            std::printf("FAIL: transparent: %.0f Hz passes at %.4f dB, not within 0.01 dB\n", hz, gain);
            ++failures;
        }
        if (hz >= 11000 && gain > -70) {
            std::printf("FAIL: transparent: %.0f Hz passes at %.1f dB, not 70 dB down\n", hz, gain);
            ++failures;
        }
        if (worst > -90) {
            std::printf("FAIL: transparent: an image of %.0f Hz at %.0f Hz is at %.1f dB, above -90\n", hz, worst_hz,
                        worst);
            ++failures;
        }
    }
}

// Windows' converter (the identified filter; recordings of music agree in band levels and
// images): the pass band, the roll-off and the images at 22 050 - f.
void check_windows() {
    struct Tone {
        double hz, gain_low, gain_high, image_max;
    };
    const Tone tones[] = {{1000, -0.16, 0.04, -70}, {8000, -0.6, -0.1, -40}, {8500, -1.3, -0.7, -50},
                          {9750, -5.8, -4.8, -45}, {10250, -9.0, -7.9, -27}, {10900, -15.0, -13.8, -15}};
    for (const auto &t : tones) {
        const auto out = convert<WindowsResampler>(tone(t.hz));
        const double gain = tone_db(out, t.hz), image = tone_db(out, 22050 - t.hz);
        std::printf("windows: %.0f Hz at %.2f dB, its image at %.0f Hz at %.1f dB\n", t.hz, gain, 22050 - t.hz, image);
        if (gain < t.gain_low || gain > t.gain_high) {
            std::printf("FAIL: windows: %.0f Hz passes at %.2f dB, Windows' converter at %.2f..%.2f\n", t.hz, gain,
                        t.gain_low, t.gain_high);
            ++failures;
        }
        if (image > t.image_max) {
            std::printf("FAIL: windows: the image of %.0f Hz is at %.1f dB, Windows' at most %.0f\n", t.hz, image,
                        t.image_max);
            ++failures;
        }
    }
}

// 2 s in, 2 * 48 000 frames out but the filter's look-ahead at the end.
template <class Converter> void check_length(const char *name, std::size_t ahead) {
    const auto frames = convert<Converter>(std::vector<std::int16_t>(2 * 2 * 22050, 0)).size() / 2;
    const auto missing = 96000 - std::min<std::size_t>(frames, 96000);
    if (frames > 96000 || missing > ahead * 320 / 147 + 1) {
        std::printf("FAIL: %s: %zu frames out of 44 100 in\n", name, frames);
        ++failures;
    }
}

} // namespace

int main() {
    check_transparent();
    check_windows();
    check_length<TransparentResampler>("transparent", TransparentResampler::half);
    check_length<WindowsResampler>("windows", 6);
    if (failures)
        return 1;
    std::printf("resampler: passed\n");
    return 0;
}
