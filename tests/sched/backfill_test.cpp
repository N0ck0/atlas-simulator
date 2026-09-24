// Shadow time has hand-computable answers, so it is tested directly against
// them rather than through a simulation whose result would restate the
// implementation.

#include "atlas/sched/backfill.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/simulator.hpp"
#include "atlas/metrics/percentiles.hpp"
#include "atlas/metrics/time_sample.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/node.hpp"
#include "atlas/model/profiles.hpp"
#include "atlas/model/workload.hpp"
#include "atlas/sched/fifo_queue.hpp"
#include "atlas/sched/queue_factory.hpp"

namespace atlas {
namespace {

constexpr Resources kNodeCapacity{16u, 65'536u};

Node busy_node(std::uint32_t id, Resources free) {
    return Node{NodeId{id}, kNodeCapacity, free, 1};
}

// One node, fully occupied by two 8-core jobs ending 1000s apart.
TEST(ShadowTime, WaitsForAsManyFinishesAsTheRequestNeeds) {
    const std::vector<Node> nodes{busy_node(0, Resources{0u, 0u})};
    const std::vector<RunningJob> running{
        RunningJob{JobId{1}, NodeId{0}, Resources{8u, 32'768u}, 1000},
        RunningJob{JobId{2}, NodeId{0}, Resources{8u, 32'768u}, 2000}};

    // Eight cores are free the moment the first job ends.
    EXPECT_EQ(shadow_time(Resources{8u, 32'768u}, nodes, running, 0), 1000u);
    // Sixteen needs both.
    EXPECT_EQ(shadow_time(Resources{16u, 65'536u}, nodes, running, 0), 2000u);
}

TEST(ShadowTime, TakesTheEarliestNode) {
    const std::vector<Node> nodes{busy_node(0, Resources{0u, 0u}), busy_node(1, Resources{0u, 0u})};
    const std::vector<RunningJob> running{RunningJob{JobId{1}, NodeId{0}, kNodeCapacity, 5000},
                                          RunningJob{JobId{2}, NodeId{1}, kNodeCapacity, 3000}};

    EXPECT_EQ(shadow_time(kNodeCapacity, nodes, running, 0), 3000u);
}

TEST(ShadowTime, IsNowWhenTheRequestAlreadyFits) {
    const std::vector<Node> nodes{busy_node(0, Resources{8u, 32'768u})};
    const std::vector<RunningJob> running{
        RunningJob{JobId{1}, NodeId{0}, Resources{8u, 32'768u}, 9000}};

    EXPECT_EQ(shadow_time(Resources{4u, 8'192u}, nodes, running, 700), 700u);
}

// The blocking dimension is memory, not cores: twelve cores are already free.
TEST(ShadowTime, RespectsEveryDimension) {
    const std::vector<Node> nodes{busy_node(0, Resources{12u, 16'384u})};
    const std::vector<RunningJob> running{
        RunningJob{JobId{1}, NodeId{0}, Resources{4u, 49'152u}, 500}};

    EXPECT_EQ(shadow_time(Resources{4u, 32'768u}, nodes, running, 0), 500u);
}

TEST(ShadowTime, IsNeverWhenNoNodeCouldEverHoldIt) {
    const std::vector<Node> nodes{busy_node(0, Resources{0u, 0u})};
    const std::vector<RunningJob> running{RunningJob{JobId{1}, NodeId{0}, kNodeCapacity, 1000}};

    // Thirty-two cores exceed the node's capacity, not merely what is free.
    EXPECT_EQ(shadow_time(Resources{32u, 65'536u}, nodes, running, 0), kNever);
}

class BackfillTest : public ::testing::Test {
protected:
    // Node 0 holds a 16-core job until t=1000, so the 16-core head is blocked.
    // Node 1 has twelve cores free, which is where a candidate would go.
    std::vector<Node> nodes_{busy_node(0, Resources{0u, 0u}),
                             busy_node(1, Resources{12u, 49'152u})};
    std::vector<RunningJob> running_{RunningJob{JobId{0}, NodeId{0}, kNodeCapacity, 1000},
                                     RunningJob{JobId{1}, NodeId{1}, Resources{4u, 16'384u}, 4000}};

    BackfillQueuePolicy policy_;

    QueuedJob head() const { return QueuedJob{JobId{10}, kNodeCapacity, 5000, 0}; }
};

TEST_F(BackfillTest, AdmitsAJobThatFinishesBeforeTheReservation) {
    // Shadow is 1000. This candidate claims 400 and fits on node 1.
    const std::vector<QueuedJob> backlog{head(), QueuedJob{JobId{11}, {4u, 8'192u}, 400, 0}};
    EXPECT_EQ(policy_.backfill(backlog, nodes_, running_, 0), JobId{11});
}

TEST_F(BackfillTest, RefusesAJobThatWouldOverrunTheReservation) {
    // Fits on node 1, but claims 1500 against a shadow of 1000. Admitting it
    // is exactly the starvation this policy exists to prevent.
    const std::vector<QueuedJob> backlog{head(), QueuedJob{JobId{11}, {4u, 8'192u}, 1500, 0}};
    EXPECT_EQ(policy_.backfill(backlog, nodes_, running_, 0), std::nullopt);
}

TEST_F(BackfillTest, RefusesAJobThatFitsNowhere) {
    // Claims only 100, but sixteen cores are free on no node.
    const std::vector<QueuedJob> backlog{head(), QueuedJob{JobId{11}, kNodeCapacity, 100, 0}};
    EXPECT_EQ(policy_.backfill(backlog, nodes_, running_, 0), std::nullopt);
}

TEST_F(BackfillTest, TakesTheFirstEligibleInQueueOrder) {
    const std::vector<QueuedJob> backlog{head(),
                                         QueuedJob{JobId{11}, {4u, 8'192u}, 1500, 0},  // too long
                                         QueuedJob{JobId{12}, {4u, 8'192u}, 300, 0},   // eligible
                                         QueuedJob{JobId{13}, {4u, 8'192u}, 100, 0}};  // also
    EXPECT_EQ(policy_.backfill(backlog, nodes_, running_, 0), JobId{12});
}

TEST_F(BackfillTest, NeverReturnsTheHeadItself) {
    const std::vector<QueuedJob> backlog{head()};
    EXPECT_EQ(policy_.backfill(backlog, nodes_, running_, 0), std::nullopt);
}

// A queue wedged behind an unsatisfiable head stays wedged. Backfilling freely
// would be safe -- there is no reservation to violate -- but it would also
// erase the censored-count signature that a capacity mismatch shows up as.
TEST_F(BackfillTest, StaysWedgedBehindAnUnsatisfiableHead) {
    const std::vector<QueuedJob> backlog{QueuedJob{JobId{10}, {32u, 65'536u}, 5000, 0},
                                         QueuedJob{JobId{11}, {4u, 8'192u}, 100, 0}};
    EXPECT_EQ(policy_.backfill(backlog, nodes_, running_, 0), std::nullopt);
}

TEST(FifoQueuePolicy, NeverLetsAnythingJump) {
    FifoQueuePolicy fifo;
    const std::vector<Node> nodes{busy_node(0, kNodeCapacity)};
    const std::vector<QueuedJob> backlog{QueuedJob{JobId{0}, {32u, 0u}, 10, 0},
                                         QueuedJob{JobId{1}, {1u, 1u}, 1, 0}};
    EXPECT_EQ(fifo.backfill(backlog, nodes, {}, 0), std::nullopt);
}

TEST(QueueFactory, ConstructsEveryListedNameAndNothingElse) {
    for (std::string_view name : queue_policy_names()) {
        const auto policy = make_queue_policy(name);
        ASSERT_NE(policy, nullptr) << name;
        EXPECT_EQ(policy->name(), name);
    }
    EXPECT_EQ(make_queue_policy("nope"), nullptr);
}

// End to end, because the interesting claims are about what the two policies
// do to a whole run rather than to one decision.
struct RunOutcome {
    std::size_t backfilled = 0;
    Tick p95_queue_wait = 0;
};

RunOutcome run_with(std::string_view queue) {
    Cluster cluster{8u, kDefaultResources};
    WorkloadGenerator gen(7u, 0.025);
    Simulator sim(std::move(cluster), gen.generate(500), nullptr, nullptr,
                  make_queue_policy(queue));
    sim.run();

    std::vector<Tick> waits = queue_wait_times(sim.jobs()).times;
    EXPECT_FALSE(waits.empty());
    return RunOutcome{sim.backfilled_count(), percentiles(waits).p95};
}

TEST(BackfillEndToEnd, FifoNeverBackfillsAndBackfillDoes) {
    EXPECT_EQ(run_with("fifo").backfilled, 0u);
    EXPECT_GT(run_with("backfill").backfilled, 0u);
}

TEST(BackfillEndToEnd, CutsQueueWaitAgainstFifoOnTheSameWorkload) {
    EXPECT_LT(run_with("backfill").p95_queue_wait, run_with("fifo").p95_queue_wait);
}

// Two runs of the same seed must agree exactly, backfill included: the policy
// reads estimates that are drawn from the seeded generator, so nothing about
// its decisions may depend on anything else.
TEST(BackfillEndToEnd, IsDeterministic) {
    const RunOutcome a = run_with("backfill");
    const RunOutcome b = run_with("backfill");
    EXPECT_EQ(a.backfilled, b.backfilled);
    EXPECT_EQ(a.p95_queue_wait, b.p95_queue_wait);
}

}  // namespace
}  // namespace atlas
