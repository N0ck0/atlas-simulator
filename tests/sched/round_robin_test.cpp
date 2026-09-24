// RoundRobin: spread placements across nodes instead of always starting the
// scan from index 0.
//
// TODO(nick): implement src/atlas/sched/round_robin.{hpp,cpp}, add "round_robin"
// to the factory's table and kNames, add the .cpp to atlas_lib, and uncomment
// this file in tests/CMakeLists.txt.
//
// Universal guarantees -- never returning a node that cannot fit, nullopt when
// nothing fits, determinism -- are covered for every scheduler in
// scheduler_contract_test.cpp. What is here is only what makes RoundRobin
// different from the others.

#include "atlas/sched/round_robin.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <set>

#include "atlas/engine/ids.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {
namespace {

constexpr Resources kPerNode{8u, 16'384u};
constexpr std::uint32_t kNodeCount = 4u;

Job tiny_job() {
    Job j{};
    j.request = Resources{1u, 1'024u};
    return j;
}

class RoundRobinTest : public ::testing::Test {
protected:
    Cluster cluster_{kNodeCount, kPerNode};
    RoundRobinScheduler scheduler_;
};

// The defining property. FirstFit would answer node 0 every time here, because
// every node still fits after a tiny allocation.
TEST_F(RoundRobinTest, VisitsEveryNodeBeforeRepeatingOne) {
    std::set<std::uint32_t> visited;

    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        const std::optional<NodeId> placed = scheduler_.place(tiny_job(), cluster_.nodes_span());
        ASSERT_TRUE(placed.has_value()) << "at placement " << i;
        visited.insert(static_cast<std::uint32_t>(*placed));
        cluster_.allocate(*placed, tiny_job().request);
    }

    EXPECT_EQ(visited.size(), kNodeCount) << "a node was reused before the cycle completed";
}

// The cursor advances per placement returned, not per allocation: place() is
// never told whether the caller went on to allocate.
TEST_F(RoundRobinTest, AdvancesEvenWhenTheClusterIsUntouched) {
    const std::optional<NodeId> first = scheduler_.place(tiny_job(), cluster_.nodes_span());
    const std::optional<NodeId> second = scheduler_.place(tiny_job(), cluster_.nodes_span());

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
}

TEST_F(RoundRobinTest, WrapsAroundAfterTheLastNode) {
    std::vector<NodeId> sequence;
    for (std::uint32_t i = 0; i < kNodeCount * 2u; ++i) {
        const std::optional<NodeId> placed = scheduler_.place(tiny_job(), cluster_.nodes_span());
        ASSERT_TRUE(placed.has_value()) << "at placement " << i;
        sequence.push_back(*placed);
    }

    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        EXPECT_EQ(sequence[i], sequence[i + kNodeCount]) << "cycle did not repeat at offset " << i;
    }
}

// Round robin is a preference, not an obligation: a node whose turn it is but
// which cannot hold the job has to be passed over rather than returned.
TEST_F(RoundRobinTest, SkipsNodesThatCannotFit) {
    cluster_.allocate(NodeId{0}, kPerNode);
    cluster_.allocate(NodeId{2}, kPerNode);

    for (std::uint32_t i = 0; i < 6u; ++i) {
        const std::optional<NodeId> placed = scheduler_.place(tiny_job(), cluster_.nodes_span());
        ASSERT_TRUE(placed.has_value()) << "at placement " << i;
        EXPECT_NE(*placed, NodeId{0});
        EXPECT_NE(*placed, NodeId{2});
    }
}

TEST_F(RoundRobinTest, IdentifiesItselfAsRoundRobin) {
    EXPECT_EQ(scheduler_.name(), "round_robin");
}

}  // namespace
}  // namespace atlas
