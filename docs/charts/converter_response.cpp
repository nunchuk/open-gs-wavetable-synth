// SPDX-License-Identifier: MIT
// The data of README.md's converter chart (docs/charts/converter_response.csv): a 2 s tone at half
// scale goes into each 22 050 -> 48 000 Hz converter; for each tone frequency, the level of the tone
// that comes out and of its loudest image (22 050 k -+ f for k = 1..4, folded into 0..24 kHz, but
// those that fold onto the tone itself, within 20 Hz), in dB re the tone that went in
// (Hann-windowed DFT of the left channel, the edges left out).
//   c++ -std=c++20 -O2 -Iinclude docs/charts/converter_response.cpp -o /tmp/converter_response
//   /tmp/converter_response > docs/charts/converter_response.csv
#include "gswavetable/resampler.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

constexpr double pi = std::numbers::pi;

std::vector<std::int16_t> tone(double hz) {
    std::vector<std::int16_t> pcm(2 * 2 * 22050);
    for (std::size_t i = 0; i < pcm.size() / 2; ++i)
        pcm[2 * i] = pcm[2 * i + 1] = std::int16_t(std::lround(16383.5 * std::sin(2 * pi * hz * double(i) / 22050)));
    return pcm;
}

template <class Converter> std::vector<float> convert(const std::vector<std::int16_t> &pcm, std::size_t ahead) {
    Converter c;
    c.push(pcm.data(), pcm.size() / 2);
    const std::vector<std::int16_t> silence(2 * ahead, 0);
    c.push(silence.data(), ahead);
    std::vector<float> out(2 * 96000);
    out.resize(2 * c.pull(out.data(), 96000));
    return out;
}

double level_db(const std::vector<float> &stereo, double hz) {
    const std::size_t skip = 4800, n = stereo.size() / 2 - 2 * skip;
    std::complex<double> sum = 0, step = std::polar(1.0, -2 * pi * hz / 48000.0), phase = std::polar(1.0, -2 * pi * hz * double(skip) / 48000.0);
    double weight = 0;
    for (std::size_t i = 0; i < n; ++i, phase *= step) {
        const double w = 0.5 - 0.5 * std::cos(2 * pi * double(i) / double(n - 1));
        sum += w * double(stereo[2 * (skip + i)]) * phase;
        weight += w;
    }
    return 20 * std::log10(2 * std::abs(sum) / weight / 0.5 + 1e-15);
}

double folded(double hz) {
    hz = std::fmod(std::abs(hz), 48000.0);
    return hz > 24000 ? 48000 - hz : hz;
}

void measure(const std::vector<float> &out, double hz, double &gain, double &image) {
    gain = level_db(out, hz);
    image = -300;
    for (int k = 1; k <= 4; ++k)
        for (const double f : {22050.0 * k - hz, 22050.0 * k + hz})
            if (std::abs(folded(f) - hz) >= 20) // an image on the tone cannot be told from it
                image = std::max(image, level_db(out, folded(f)));
}

} // namespace

int main() {
    std::printf("hz,transparent_tone_db,transparent_image_db,windows_tone_db,windows_image_db\n");
    std::vector<double> frequencies;
    for (double hz = 100; hz < 8000; hz += 100)
        frequencies.push_back(hz);
    for (double hz = 8000; hz <= 11000; hz += 25)
        frequencies.push_back(hz);
    for (const double hz : frequencies) {
        const auto pcm = tone(hz);
        double tg, ti, wg, wi;
        measure(convert<gswavetable::TransparentResampler>(pcm, gswavetable::TransparentResampler::half), hz, tg, ti);
        measure(convert<gswavetable::WindowsResampler>(pcm, 6), hz, wg, wi);
        std::printf("%.0f,%.4f,%.2f,%.4f,%.2f\n", hz, tg, ti, wg, wi);
    }
}
