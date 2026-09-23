#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <random>

#include "atlas/engine/clock.hpp"

namespace atlas {

// Deterministic source of workload randomness.
//
// Uses the engine's raw output with hand-rolled transforms instead of
// <random>'s distribution classes. The standard pins mt19937_64's output
// sequence exactly, but leaves each distribution's mapping implementation
// defined, so std::exponential_distribution yields different values on
// libstdc++ and libc++ from the same seed. Hand-rolling keeps golden traces
// portable across toolchains.
class RandomSource {
public:
    explicit RandomSource(std::uint64_t seed) : rng_(seed) {}

    // Uniform in (0, 1]. Zero is excluded so callers may take a logarithm.
    double next_uniform() {
        static constexpr double kPow53 = static_cast<double>(1ULL << 53);
        std::uint64_t random = rng_();
        random >>= 11;
        random++;
        return static_cast<double>(random) / kPow53;
    }

    // Exponentially distributed gap with the given mean, in Ticks. Returns 0
    // when a draw rounds below one microsecond; simultaneity is handled by the
    // (time, seq) tie-break.
    // Precondition: mean_ticks > 0.
    Tick next_exponential(double mean_ticks) {
        assert(mean_ticks > 0.0);

        // Inverse transform sampling: for U uniform on (0, 1], -ln(U) * mean
        // is exponentially distributed with that mean. next_uniform() excludes
        // zero, so the logarithm is always defined and the result is never
        // negative.
        const double gap_ticks = -std::log(next_uniform()) * mean_ticks;
        return static_cast<Tick>(gap_ticks);
    }

    // Time until the next event in a Poisson process of `rate_per_second`.
    // Precondition: rate_per_second > 0.
    Tick next_interarrival(double rate_per_second) {
        assert(rate_per_second > 0.0);
        return next_exponential((1 / rate_per_second) * static_cast<double>(kSecond));
    }

    // Uniform integer in [0, n). Carries negligible modulo bias at the scales
    // this is used for (choosing among a handful of job profiles).
    // Precondition: n > 0.
    std::uint64_t next_below(std::uint64_t n) {
        assert(n > 0);
        return rng_() % n;
    }

private:
    std::mt19937_64 rng_;
};

}  // namespace atlas
