// Worked example for the suite: a pure function with sharp edges.
//
// Nearest-rank is a convention, not a law -- other definitions interpolate
// between neighbours and would give different answers on the same sample. These
// cases pin the convention down by example, so a later change to percentiles()
// fails here rather than quietly shifting every cross-scheduler comparison.

#include "atlas/metrics/percentiles.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "atlas/engine/clock.hpp"

namespace atlas {
namespace {

// 1..100, so the pth percentile is exactly p under nearest-rank and an
// off-by-one in the index is impossible to miss.
std::vector<Tick> one_to_hundred() {
    std::vector<Tick> values;
    values.reserve(100);
    for (Tick i = 1; i <= 100; ++i) {
        values.push_back(i);
    }
    return values;
}

TEST(Percentiles, NearestRankPicksTheSmallestValueAtOrAboveTheRank) {
    std::vector<Tick> values = one_to_hundred();
    const Percentiles p = percentiles(values);

    // p99 of 100 samples is the 99th value, not the largest. Interpolating
    // conventions would answer 99.01 or 100 here.
    EXPECT_EQ(p.p50, 50u);
    EXPECT_EQ(p.p95, 95u);
    EXPECT_EQ(p.p99, 99u);
}

TEST(Percentiles, SortsTheInputSoOrderDoesNotMatter) {
    std::vector<Tick> ascending = one_to_hundred();
    std::vector<Tick> descending(ascending.rbegin(), ascending.rend());

    const Percentiles from_ascending = percentiles(ascending);
    const Percentiles from_descending = percentiles(descending);

    EXPECT_EQ(from_ascending.p50, from_descending.p50);
    EXPECT_EQ(from_ascending.p95, from_descending.p95);
    EXPECT_EQ(from_ascending.p99, from_descending.p99);
}

TEST(Percentiles, ReordersItsArgumentInPlace) {
    std::vector<Tick> values{30, 10, 20};
    percentiles(values);

    // Documented behaviour, not an accident: callers that still need the
    // original order have to pass a copy.
    EXPECT_TRUE(std::is_sorted(values.begin(), values.end()));
}

TEST(Percentiles, SingleSampleIsEveryPercentile) {
    std::vector<Tick> values{42};
    const Percentiles p = percentiles(values);

    EXPECT_EQ(p.p50, 42u);
    EXPECT_EQ(p.p95, 42u);
    EXPECT_EQ(p.p99, 42u);
}

}  // namespace
}  // namespace atlas
