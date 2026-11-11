// SPDX-License-Identifier: MIT
// How Windows carries the GS synth's 22 050 Hz stream to a 48 kHz endpoint: the audio engine's
// sample-rate converter, identified from recordings in which both the synth's own output and the
// endpoint's loopback (Windows Server 2022, a virtual audio cable at 48 000 Hz float) of the same
// play exist. Fitted by least squares per output phase, the loopback is a periodic FIR of the stream
// to -144.8 dB (float precision) on held-out data, the same on both channels and on two songs: 320
// output phases for every 147 input frames, each output frame the sum of the 12 input frames around
// its position weighted by a Kaiser-windowed sinc that spans 5 zero crossings each side:
//   h(u) = a sinc(fc u) I0(beta sqrt(1 - (fc u / 5)^2)) / I0(beta)  for |fc u| <= 5, else 0,
// u the distance in input frames, fc = 0.897763014 (the sinc's cutoff, 9 897.8 Hz), a = 0.892006103
// (no per-phase normalisation: the DC gain ripples 0.996-1.003 with the phase, as Windows'),
// beta = 3.2051713; the fitted curve leaves 8.8e-9 rms of the 3 565 identified coefficients.
// Against a tone this passes 1 kHz at -0.06 dB, rolls off from 8 kHz (-0.3 dB) through 10.25 kHz
// (-8.4 dB) to 10.9 kHz (-14.4 dB), and puts the image of 8.5 / 9.75 kHz at -57 / -50 dB.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gswavetable {

class Resampler {
  public:
    static constexpr int up = 320, down = 147; // 48 000 / 22 050
    static constexpr int taps = 12;            // input frames -5 .. +6 around each output position

    // Output frame k sits at input frame (147 k + offset) / 320: 0 puts output frame 0 on input
    // frame 0; another offset moves the grid (to line it up with a recording, say).
    explicit Resampler(std::int64_t offset = 0) : offset_(offset) {
        constexpr double a = 0.892006103, fc = 0.897763014, beta = 3.2051713, pi = 3.14159265358979323846;
        const auto i0 = [](double z) { // the modified Bessel function of the first kind, order 0
            double sum = 1, term = 1;
            for (int k = 1; k < 40; ++k) {
                term *= (z / 2) / k;
                sum += term * term;
            }
            return sum;
        };
        for (int p = 0; p < up; ++p)
            for (int t = 0; t < taps; ++t) {
                const double x = fc * (double(t - 5) - double(p) / up);
                double h = 0;
                if (std::abs(x) <= 5) {
                    const double sinc = x == 0 ? 1.0 : std::sin(pi * x) / (pi * x);
                    h = a * sinc * i0(beta * std::sqrt(1 - (x / 5) * (x / 5))) / i0(beta);
                }
                coefficients_[std::size_t(p)][std::size_t(t)] = float(h);
            }
        first_ = std::min<std::int64_t>(0, floor_div(offset_, up)) - 5; // silence before input frame 0
        input_.assign(std::size_t(-2 * first_), 0.0F);
    }

    // Appends 16-bit stereo input frames.
    void push(const std::int16_t *stereo, std::size_t frames) {
        for (std::size_t i = 0; i < 2 * frames; ++i)
            input_.push_back(float(stereo[i]) / 32768.0F);
    }

    // Output frames that the input pushed so far completes.
    [[nodiscard]] std::size_t available() const noexcept {
        const std::int64_t last = first_ + std::int64_t(input_.size() / 2) - 1; // the last input frame
        // the largest k with floor((147 k + offset) / 320) + 6 <= last
        const std::int64_t room = (last - 6 + 1) * up - 1 - offset_;
        if (room < 0)
            return 0;
        const auto k = std::uint64_t(room / down) + 1; // frames 0 .. room / 147
        return k > made_ ? std::size_t(k - made_) : 0;
    }

    // Writes up to `frames` stereo float frames; returns how many it wrote.
    std::size_t pull(float *stereo, std::size_t frames) {
        const auto n = std::min(frames, available());
        for (std::size_t i = 0; i < n; ++i, ++made_) {
            const std::int64_t q = std::int64_t(made_) * down + offset_;
            const std::int64_t base = floor_div(q, up);
            const auto &c = coefficients_[std::size_t(q - base * up)];
            const float *x = input_.data() + 2 * (base - 5 - first_);
            float left = 0, right = 0;
            for (int t = 0; t < taps; ++t) {
                left += c[std::size_t(t)] * x[2 * t];
                right += c[std::size_t(t)] * x[2 * t + 1];
            }
            stereo[2 * i] = left;
            stereo[2 * i + 1] = right;
        }
        // Drop the input no later frame needs.
        const std::int64_t keep_from = floor_div(std::int64_t(made_) * down + offset_, up) - 5;
        if (keep_from - first_ > 4096) {
            input_.erase(input_.begin(), input_.begin() + std::ptrdiff_t(2 * (keep_from - first_)));
            first_ = keep_from;
        }
        return n;
    }

  private:
    static std::int64_t floor_div(std::int64_t a, std::int64_t b) {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }
    std::array<std::array<float, taps>, up> coefficients_{};
    std::int64_t offset_;
    std::vector<float> input_; // interleaved stereo from input frame first_
    std::int64_t first_ = 0;
    std::uint64_t made_ = 0;
};

} // namespace gswavetable
