// SPDX-License-Identifier: MIT
// The engine's tables (computed with pow, log10 and sin at start-up) against their known hash, and
// the pitch and volume conversions at a few points. The hash is of tables that equal the ones
// wdmaud.drv computes (checked entry by entry against a dump of its .data section); a different
// hash on a platform means its libm or a floating-point contraction flipped a rounding.
#include "gswavetable/engine.hpp"

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

int failures = 0;

void expect(bool ok, const char *what, long long got, long long want) {
    if (!ok) {
        std::printf("FAIL: %s: %lld, expected %lld\n", what, got, want);
        ++failures;
    }
}

// FNV-1a 64 over every array in declaration order, each element little-endian.
std::uint64_t tables_hash() {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    const auto feed = [&h](const auto &array) {
        for (const auto value : array) {
            using T = std::remove_cv_t<decltype(value)>;
            const auto bits = static_cast<std::make_unsigned_t<T>>(value);
            for (std::size_t b = 0; b < sizeof(T); ++b) {
                h ^= std::uint8_t(bits >> (8 * b));
                h *= 0x100000001b3ULL;
            }
        }
    };
    const auto &t = gswavetable::tables();
    feed(t.vfract);
    feed(t.pitch_fine);
    feed(t.pitch_semi);
    feed(t.attack);
    feed(t.sine);
    feed(t.pan);
    return h;
}

} // namespace

int main() {
    constexpr std::uint64_t expected = 0xa71c7a8d3fdda710ULL;
    const auto hash = tables_hash();
    std::printf("tables: FNV-1a 64 0x%016llx\n", static_cast<unsigned long long>(hash));
    if (hash != expected) {
        std::printf("FAIL: the tables' hash is not 0x%016llx\n", static_cast<unsigned long long>(expected));
        ++failures;
    }

    struct Point {
        std::int32_t in, out;
    };
    // CDigitalAudio::PRELToPFRACT: cents to a 4.12 pitch ratio, clamped to +-4800 cents.
    for (const auto p : {Point{-4800, 256}, Point{-1300, 1933}, Point{-1200, 2048}, Point{-101, 3863},
                         Point{-100, 3866}, Point{-1, 4093}, Point{0, 4096}, Point{1, 4098}, Point{100, 4339},
                         Point{101, 4341}, Point{1199, 8186}, Point{1200, 8192}, Point{2500, 17356},
                         Point{4800, 65536}, Point{9999, 65536}})
        expect(gswavetable::prel_to_pfract(p.in) == p.out, "prel_to_pfract", gswavetable::prel_to_pfract(p.in), p.out);
    // CDigitalAudio::VRELToVFRACT: 1/100 dB to 0..4095, in steps of 0.1 dB.
    for (const auto p : {Point{-20000, 0}, Point{-9601, 0}, Point{-6000, 4}, Point{-605, 2052}, Point{-10, 4048},
                         Point{-9, 4095}, Point{0, 4095}, Point{5, 4095}})
        expect(gswavetable::vrel_to_vfract(p.in) == p.out, "vrel_to_vfract", gswavetable::vrel_to_vfract(p.in), p.out);

    if (failures)
        return 1;
    std::printf("tables: passed\n");
    return 0;
}
