// Determinism, range, and distribution shape. Randomness is tested by its
// statistics, never by exact values, so each tolerance below is stated in
// sigmas: tight enough to catch a wrong transform, loose enough that it will
// not fail by chance.

#include "atlas/model/random.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "atlas/engine/clock.hpp"

namespace atlas {
namespace {

constexpr std::uint64_t kSeed = 42;
constexpr std::size_t kSamples = 200'000;
constexpr double kRate = 0.025;  // jobs per second -> mean gap 100'000 us

// The property every reproducible experiment rests on.
TEST(RandomSource, SameSeedReplaysAnIdenticalSequence) {
    RandomSource a(kSeed);
    RandomSource b(kSeed);

    for (std::size_t i = 0; i < 1'000; ++i) {
        ASSERT_EQ(a.next_uniform(), b.next_uniform()) << "diverged at draw " << i;
    }
}

// Otherwise the seed is being ignored and every "deterministic" result is the
// same result.
TEST(RandomSource, ADifferentSeedProducesADifferentSequence) {
    RandomSource a(kSeed);
    RandomSource b(kSeed + 1);

    bool any_difference = false;
    for (std::size_t i = 0; i < 1'000; ++i) {
        if (a.next_uniform() != b.next_uniform()) any_difference = true;
    }
    EXPECT_TRUE(any_difference);
}

// (0, 1]. Zero would make the logarithm in next_exponential undefined.
TEST(RandomSource, EveryUniformDrawLiesInTheOpenUnitInterval) {
    RandomSource r(kSeed);

    for (std::size_t i = 0; i < kSamples; ++i) {
        const double u = r.next_uniform();
        ASSERT_GT(u, 0.0) << "at draw " << i;
        ASSERT_LE(u, 1.0) << "at draw " << i;
    }
}

TEST(RandomSource, InterarrivalGapsAreExponentialWithTheConfiguredMean) {
    RandomSource r(kSeed);

    double sum = 0.0;
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < kSamples; ++i) {
        const double gap = static_cast<double>(r.next_interarrival(kRate));
        ASSERT_GE(gap, 0.0) << "negative gap at draw " << i;
        sum += gap;
        sum_sq += gap * gap;
    }

    const double n = static_cast<double>(kSamples);
    const double mean = sum / n;
    const double stddev = std::sqrt(sum_sq / n - mean * mean);
    const double expected_mean = 1e6 / kRate;

    // Relative standard error of the mean is 1/sqrt(N) ~ 0.22%, so a 2% band
    // is about 9 sigma.
    EXPECT_NEAR(mean, expected_mean, 0.02 * expected_mean);

    // The exponential distribution has stddev == mean, so its coefficient of
    // variation is 1. A uniform distribution with the correct mean would give
    // ~0.577, so this tests the SHAPE rather than just the average.
    const double cv = stddev / mean;
    EXPECT_GT(cv, 0.95) << "coefficient of variation " << cv;
    EXPECT_LT(cv, 1.05) << "coefficient of variation " << cv;
}

TEST(RandomSource, NextBelowStaysInRangeAndCoversEvenly) {
    constexpr std::uint64_t kBuckets = 5;
    constexpr std::size_t kDraws = 100'000;

    RandomSource r(kSeed);
    std::vector<std::size_t> counts(kBuckets, 0);
    for (std::size_t i = 0; i < kDraws; ++i) {
        const std::uint64_t v = r.next_below(kBuckets);
        ASSERT_LT(v, kBuckets) << "out of range at draw " << i;
        ++counts[v];
    }

    // Expected 20'000 per bucket, sigma ~126, so a 10% band is ~16 sigma.
    for (std::size_t i = 0; i < kBuckets; ++i) {
        const double share = static_cast<double>(counts[i]) / static_cast<double>(kDraws);
        EXPECT_NEAR(share, 0.20, 0.02) << "bucket " << i;
    }
}

TEST(RandomSource, NextBelowOneIsAlwaysZero) {
    RandomSource r(kSeed);
    EXPECT_EQ(r.next_below(1), 0u);
}

}  // namespace
}  // namespace atlas
