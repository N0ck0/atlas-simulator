// The workload must be a pure function of the seed, structurally well-formed,
// and calibrated to the configured arrival rate.

#include "atlas/model/workload.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/metrics/load_factor.hpp"
#include "atlas/metrics/offered_load.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/profiles.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {
namespace {

constexpr std::uint64_t kSeed = 7;
constexpr std::uint32_t kCount = 20'000;
constexpr double kRate = 0.025;  // jobs per second

std::vector<Job> make_workload(std::uint64_t seed = kSeed, double rate = kRate) {
    WorkloadGenerator gen(seed, rate);
    return gen.generate(kCount);
}

TEST(WorkloadGenerator, SameSeedProducesAnIdenticalWorkload) {
    EXPECT_EQ(make_workload(), make_workload());
}

TEST(WorkloadGenerator, ADifferentSeedProducesADifferentWorkload) {
    EXPECT_NE(make_workload(kSeed + 1), make_workload(kSeed));
}

TEST(WorkloadGenerator, GeneratesTheRequestedCount) { EXPECT_EQ(make_workload().size(), kCount); }

TEST(WorkloadGenerator, EveryJobIsWellFormedAndFresh) {
    const std::vector<Job> jobs = make_workload();
    ASSERT_EQ(jobs.size(), kCount);

    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const Job& j = jobs[i];
        SCOPED_TRACE(testing::Message() << "job index " << i);

        // Simulator::job() indexes jobs_ by id, so dense ids are a
        // precondition of the whole run, not a cosmetic property.
        EXPECT_EQ(static_cast<std::size_t>(static_cast<std::uint32_t>(j.id)), i);
        if (i > 0) {
            EXPECT_GE(j.submit_time, jobs[i - 1].submit_time);
        }
        EXPECT_EQ(j.state, JobState::Queued);
        EXPECT_EQ(j.start_time, kNever);
        EXPECT_EQ(j.finish_time, kNever);
        EXPECT_GT(j.duration, 0u);
        EXPECT_GT(j.request.cores, 0u);
    }
}

// Rate calibration read back off the submit times, rather than trusting the
// generator's own arithmetic.
TEST(WorkloadGenerator, MeanArrivalGapMatchesTheConfiguredRate) {
    const std::vector<Job> jobs = make_workload();
    ASSERT_GT(jobs.size(), 1u);

    const double span = static_cast<double>(jobs.back().submit_time - jobs.front().submit_time);
    const double mean_gap = span / static_cast<double>(jobs.size() - 1);
    const double expected_gap = static_cast<double>(kSecond) / kRate;

    EXPECT_NEAR(mean_gap, expected_gap, 0.05 * expected_gap);
}

TEST(WorkloadGenerator, EveryProfileAppearsAcrossTwentyThousandDraws) {
    const std::vector<Job> jobs = make_workload();
    constexpr std::size_t kProfileCount = std::size(kJobProfiles);

    std::vector<std::size_t> seen(kProfileCount, 0);
    for (const Job& j : jobs) {
        for (std::size_t p = 0; p < kProfileCount; ++p) {
            if (j.request == kJobProfiles[p].request) ++seen[p];
        }
    }

    for (std::size_t p = 0; p < kProfileCount; ++p) {
        EXPECT_GT(seen[p], 0u) << "profile " << kJobProfiles[p].name << " never drawn";
    }
}

// Load factor is checked by its scaling properties rather than by recomputing
// the formula, which would only restate the implementation.
class OfferedLoadTest : public ::testing::Test {
protected:
    const std::vector<Job> jobs_ = make_workload();
    Cluster small_{8u, Resources{16u, 65'536u}};
};

TEST_F(OfferedLoadTest, IsPositiveAtTheDefaultOperatingPoint) {
    const LoadFactor lf = offered_load(jobs_, small_);

    EXPECT_GT(lf.cores, 0.0);
    EXPECT_GT(lf.memory, 0.0);
}

TEST_F(OfferedLoadTest, HalvesWhenClusterCapacityDoubles) {
    const LoadFactor lf = offered_load(jobs_, small_);
    Cluster big(16u, Resources{16u, 65'536u});
    const LoadFactor lf_big = offered_load(jobs_, big);

    EXPECT_NEAR(lf_big.cores, lf.cores / 2.0, 0.01 * lf.cores);
}

// The same jobs arriving over half the span must offer roughly twice the load.
TEST_F(OfferedLoadTest, ScalesWithArrivalRate) {
    const LoadFactor lf = offered_load(jobs_, small_);
    const LoadFactor lf_fast = offered_load(make_workload(kSeed, kRate * 2.0), small_);

    const double ratio = lf_fast.cores / lf.cores;
    EXPECT_GT(ratio, 1.8) << "ratio " << ratio;
    EXPECT_LT(ratio, 2.2) << "ratio " << ratio;
}

}  // namespace
}  // namespace atlas
