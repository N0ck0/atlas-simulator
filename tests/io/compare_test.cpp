#include "atlas/io/compare.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "atlas/engine/simulator.hpp"
#include "atlas/metrics/percentiles.hpp"
#include "atlas/metrics/time_sample.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/profiles.hpp"
#include "atlas/model/workload.hpp"
#include "atlas/sched/factory.hpp"
#include "atlas/sched/queue_factory.hpp"

namespace atlas {
namespace {

// Small enough to keep the suite fast; the shape claims below do not depend on
// the operating point.
Options test_options() {
    Options opt;
    opt.seed = 100;
    opt.nodes = 4;
    opt.jobs = 200;
    return opt;
}

TEST(Compare, CoversEveryConfigurationAndSeed) {
    const Comparison cmp = compare_schedulers(test_options(), 3);
    const std::size_t configs = scheduler_names().size() * queue_policy_names().size();
    ASSERT_EQ(cmp.rows.size(), configs);
    ASSERT_EQ(cmp.labels.size(), configs);
    for (const auto& row : cmp.rows) EXPECT_EQ(row.size(), 3u);
    EXPECT_EQ(cmp.seeds, 3u);
    EXPECT_EQ(cmp.base_seed, test_options().seed);
}

// The table's whole claim is that only the policy varies, so two sweeps with
// the same options must agree exactly -- floats included.
TEST(Compare, IsDeterministic) {
    const Comparison a = compare_schedulers(test_options(), 3);
    const Comparison b = compare_schedulers(test_options(), 3);
    ASSERT_EQ(a.rows.size(), b.rows.size());
    for (std::size_t i = 0; i < a.rows.size(); ++i) {
        ASSERT_EQ(a.rows[i].size(), b.rows[i].size());
        for (std::size_t k = 0; k < a.rows[i].size(); ++k) {
            EXPECT_EQ(a.rows[i][k].p95_turnaround, b.rows[i][k].p95_turnaround);
            EXPECT_EQ(a.rows[i][k].p95_queue_wait, b.rows[i][k].p95_queue_wait);
            EXPECT_EQ(a.rows[i][k].utilization.cores, b.rows[i][k].utilization.cores);
            EXPECT_EQ(a.rows[i][k].censored, b.rows[i][k].censored);
        }
    }
}

// The one that matters: a cell must be the same number a plain single run
// produces. If the sweep builds its Simulator even slightly differently, the
// table stops describing what the rest of the project measures.
TEST(Compare, CellMatchesAStandaloneRun) {
    const Options opt = test_options();
    const std::uint32_t seed_offset = 2;

    Cluster cluster{opt.nodes, kDefaultResources};
    WorkloadGenerator gen(opt.seed + seed_offset, opt.arrival_rate, opt.estimate_padding);
    Simulator sim(std::move(cluster), gen.generate(opt.jobs), nullptr,
                  make_scheduler(scheduler_names()[0]));
    sim.run();
    std::vector<Tick> times = turnaround_times(sim.jobs()).times;
    ASSERT_FALSE(times.empty());
    const Tick expected_p95 = percentiles(times).p95;

    const Comparison cmp = compare_schedulers(opt, 3);
    ASSERT_FALSE(cmp.rows.empty());
    ASSERT_EQ(cmp.rows[0].size(), 3u);
    // Row 0 is fifo/<first scheduler>, which is what the standalone run above
    // constructs by taking both defaults.
    EXPECT_EQ(cmp.labels[0], std::string("fifo/") + std::string(scheduler_names()[0]));
    EXPECT_EQ(cmp.rows[0][seed_offset].p95_turnaround, expected_p95);
    EXPECT_EQ(cmp.rows[0][seed_offset].backfilled, 0u);
}

// Offered load is a property of the workload, not the policy, so it belongs
// above the table rather than in it -- and must be non-degenerate.
TEST(Compare, OfferedLoadIsRecordedPerSeed) {
    const Comparison cmp = compare_schedulers(test_options(), 2);
    ASSERT_EQ(cmp.offered_rho.size(), 2u);
    for (const LoadFactor& rho : cmp.offered_rho) {
        EXPECT_GT(rho.cores, 0.0);
        EXPECT_GT(rho.memory, 0.0);
    }
    // Different seeds draw different workloads, so a sweep that reused one
    // seed's jobs everywhere would show up as identical rho.
    EXPECT_NE(cmp.offered_rho[0].cores, cmp.offered_rho[1].cores);
}

}  // namespace
}  // namespace atlas
