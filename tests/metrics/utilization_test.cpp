// The utilization integral and the queueing response. These are the metrics
// every result from S3 on is reported in, so a metric that moves the wrong way
// here would invalidate the comparisons rather than merely report them badly.

#include "atlas/metrics/utilization.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/simulator.hpp"
#include "atlas/metrics/load_factor.hpp"
#include "atlas/metrics/time_sample.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/resources.hpp"
#include "atlas/model/workload.hpp"

namespace atlas {
namespace {

constexpr std::uint64_t kSeed = 7;
constexpr std::uint32_t kCount = 2'000;
constexpr double kRate = 0.025;
constexpr Resources kPerNode{16u, 65'536u};

// Mean rather than a percentile: at these cluster sizes the median queue wait
// bottoms out at zero and stops discriminating, so it cannot detect an
// improvement that only affects the tail.
double mean_queue_wait_seconds(std::uint32_t nodes, double rate) {
    WorkloadGenerator gen(kSeed, rate);
    Simulator sim(Cluster(nodes, kPerNode), gen.generate(kCount));
    sim.run();

    const TimeSample sample = queue_wait_times(sim.jobs());
    EXPECT_EQ(sample.skipped, 0u) << "censored waits are not comparable";
    if (sample.times.empty()) return 0.0;

    Tick total = 0;
    for (Tick t : sample.times) total += t;
    return static_cast<double>(total) / static_cast<double>(sample.times.size()) / 1e6;
}

// With a node per job nothing queues, so each job holds its resources for
// exactly its duration and the integral collapses to a sum over the workload.
// That sum is computed here from the job list alone, without replaying the
// event loop, which is what makes this a check on the accumulation rather than
// a restatement of it.
TEST(UtilizationIntegral, EqualsTheWorkloadsResourceTimeWhenNothingQueues) {
    WorkloadGenerator gen(kSeed, kRate);
    const std::vector<Job> workload = gen.generate(kCount);

    Simulator roomy(Cluster(kCount, kPerNode), workload);
    roomy.run();

    std::uint64_t core_ticks = 0;
    std::uint64_t memory_ticks = 0;
    for (const Job& j : workload) {
        core_ticks += j.duration * j.request.cores;
        memory_ticks += j.duration * j.request.memory_mb;
    }

    const Resources cap = roomy.cluster().total_capacity();
    const double span = static_cast<double>(roomy.now());
    const double expected_cores =
        static_cast<double>(core_ticks) / (span * static_cast<double>(cap.cores));
    const double expected_memory =
        static_cast<double>(memory_ticks) / (span * static_cast<double>(cap.memory_mb));

    const LoadFactor got = roomy.mean_utilization();

    // Both sides are exact integer sums turned into doubles by a single
    // division, so the only slack needed is floating-point rounding.
    EXPECT_NEAR(got.cores, expected_cores, 1e-9 * expected_cores);
    EXPECT_NEAR(got.memory, expected_memory, 1e-9 * expected_memory);
}

// A metric that moves the wrong way here is measuring something other than
// what it claims to.
TEST(QueueingResponse, MoreNodesLowersMeanQueueTime) {
    const double small = mean_queue_wait_seconds(8u, kRate);
    const double large = mean_queue_wait_seconds(16u, kRate);

    EXPECT_LT(large, small) << "8 nodes " << small << "s, 16 nodes " << large << "s";
}

// Three equally spaced rates. If wait time were merely proportional to rho the
// two increments would match, so convexity is what separates a queueing system
// from a stopwatch. Compared as increments rather than ratios because the
// first wait can be near zero.
TEST(QueueingResponse, QueueTimeGrowsSuperlinearlyAsRhoApproachesOne) {
    const double w1 = mean_queue_wait_seconds(8u, 0.025);
    const double w2 = mean_queue_wait_seconds(8u, 0.030);
    const double w3 = mean_queue_wait_seconds(8u, 0.035);

    EXPECT_LT(w1, w2);
    EXPECT_LT(w2, w3);
    EXPECT_GT(w3 - w2, w2 - w1) << "waits: " << w1 << "s, " << w2 << "s, " << w3 << "s";
}

}  // namespace
}  // namespace atlas
