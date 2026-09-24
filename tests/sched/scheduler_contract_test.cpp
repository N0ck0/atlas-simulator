// Guarantees every Scheduler must honour, whatever policy it implements. The
// suite is parameterized over scheduler_names(), so a scheduler added to the
// factory's table is held to this contract automatically -- there is no list
// here to forget to update.
//
// What belongs here is anything a caller may assume from the interface alone.
// Anything that distinguishes one policy from another belongs in that policy's
// own test file.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "atlas/engine/ids.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/resources.hpp"
#include "atlas/sched/factory.hpp"
#include "atlas/sched/scheduler.hpp"

namespace atlas {
namespace {

constexpr Resources kPerNode{8u, 16'384u};
constexpr std::uint32_t kNodeCount = 4u;

Job job_requesting(Resources request) {
    Job j{};
    j.request = request;
    return j;
}

class SchedulerContract : public ::testing::TestWithParam<std::string_view> {
protected:
    std::unique_ptr<Scheduler> scheduler_ = make_scheduler(GetParam());
    Cluster cluster_{kNodeCount, kPerNode};
};

// A scheduler chooses; it never invents. Returning a node that cannot hold the
// job would trip the assert inside Cluster::allocate, which is a crash in debug
// and silent corruption of the free pool in release.
TEST_P(SchedulerContract, NeverReturnsANodeThatCannotFit) {
    ASSERT_NE(scheduler_, nullptr);

    // Fill nodes unevenly so that some fit and some do not.
    cluster_.allocate(NodeId{0}, Resources{8u, 16'384u});
    cluster_.allocate(NodeId{1}, Resources{6u, 8'192u});
    cluster_.allocate(NodeId{2}, Resources{2u, 15'000u});

    const Job job = job_requesting(Resources{3u, 4'096u});
    const std::optional<NodeId> placed = scheduler_->place(job, cluster_.nodes_span());

    if (placed.has_value()) {
        EXPECT_TRUE(cluster_.node(*placed).can_fit(job.request))
            << "chose node " << static_cast<std::uint32_t>(*placed) << " which cannot fit";
    }
}

TEST_P(SchedulerContract, ReturnsNulloptWhenNoNodeFits) {
    ASSERT_NE(scheduler_, nullptr);

    const Job too_big = job_requesting(Resources{kPerNode.cores + 1u, 1u});
    EXPECT_FALSE(scheduler_->place(too_big, cluster_.nodes_span()).has_value());
}

TEST_P(SchedulerContract, ReturnsNulloptOnAFullCluster) {
    ASSERT_NE(scheduler_, nullptr);

    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        cluster_.allocate(NodeId{i}, kPerNode);
    }

    EXPECT_FALSE(
        scheduler_->place(job_requesting(Resources{1u, 1u}), cluster_.nodes_span()).has_value());
}

// Invariant 4. Two instances driven through an identical sequence must agree at
// every step -- which fails the moment a policy ranks candidates by pointer
// address, iterates an unordered container, or leaves a tie unbroken.
TEST_P(SchedulerContract, TwoInstancesAgreeOnAnIdenticalCallSequence) {
    const std::unique_ptr<Scheduler> other = make_scheduler(GetParam());
    ASSERT_NE(scheduler_, nullptr);
    ASSERT_NE(other, nullptr);

    Cluster cluster_a{kNodeCount, kPerNode};
    Cluster cluster_b{kNodeCount, kPerNode};

    for (std::uint32_t step = 0; step < 12u; ++step) {
        const Job job = job_requesting(Resources{1u + (step % 3u), 2'048u});

        const std::optional<NodeId> a = scheduler_->place(job, cluster_a.nodes_span());
        const std::optional<NodeId> b = other->place(job, cluster_b.nodes_span());

        ASSERT_EQ(a.has_value(), b.has_value()) << "at step " << step;
        if (!a.has_value()) break;
        ASSERT_EQ(*a, *b) << "at step " << step;

        cluster_a.allocate(*a, job.request);
        cluster_b.allocate(*b, job.request);
    }
}

INSTANTIATE_TEST_SUITE_P(AllSchedulers, SchedulerContract, ::testing::ValuesIn(scheduler_names()),
                         // Names the ctest cases after the scheduler rather than an
                         // index, so a failure reads AllSchedulers/first_fit rather
                         // than AllSchedulers/0. The parameter is not called `info`
                         // because the macro declares one, and -Wshadow is an error.
                         [](const ::testing::TestParamInfo<std::string_view>& param) {
                             return std::string(param.param);
                         });

}  // namespace
}  // namespace atlas
