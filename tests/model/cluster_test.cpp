// Resource accounting: allocate and release must agree, and free must never
// drift away from capacity.

#include "atlas/model/cluster.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>

#include "atlas/engine/ids.hpp"
#include "atlas/model/node.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {
namespace {

constexpr Resources kPerNode{8u, 16'384u};
constexpr std::uint32_t kNodeCount = 4u;

class ClusterTest : public ::testing::Test {
protected:
    Cluster cluster_{kNodeCount, kPerNode};
};

TEST_F(ClusterTest, StartsFullyFreeAtTheSumOfItsNodes) {
    EXPECT_EQ(cluster_.size(), kNodeCount);
    EXPECT_EQ(cluster_.total_capacity(), (Resources{32u, 65'536u}));
    EXPECT_EQ(cluster_.total_free(), cluster_.total_capacity());
}

// Dimensions are independent: a request can fail on memory alone, which a
// single combined "size" comparison would miss.
TEST_F(ClusterTest, FitIsCheckedPerDimension) {
    const Node& n0 = cluster_.node(NodeId{0});

    EXPECT_TRUE(n0.can_fit(Resources{8u, 16'384u})) << "an exact fit must be accepted";
    EXPECT_FALSE(n0.can_fit(Resources{9u, 1'024u})) << "too many cores, memory to spare";
    EXPECT_FALSE(n0.can_fit(Resources{1u, 20'000u})) << "too much memory, cores to spare";
}

TEST_F(ClusterTest, AllocateCountsARunningJobAndDeductsFromFree) {
    cluster_.allocate(NodeId{0}, Resources{2u, 4'096u});

    EXPECT_EQ(cluster_.node(NodeId{0}).running_jobs, 1u);
    EXPECT_EQ(cluster_.node(NodeId{0}).free, (Resources{6u, 12'288u}));
    EXPECT_EQ(cluster_.node(NodeId{1}).free, kPerNode) << "other nodes must be untouched";
}

TEST_F(ClusterTest, AFullClusterHasNoFreeResources) {
    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        cluster_.allocate(NodeId{i}, kPerNode);
    }

    EXPECT_EQ(cluster_.total_free(), Resources{});
}

// Choosing a node belongs to the Scheduler interface; see
// tests/sched/first_fit_test.cpp. What stays here is the accounting.
TEST_F(ClusterTest, ExposesItsNodesAsAReadOnlySpan) {
    const std::span<const Node> nodes = cluster_.nodes_span();

    ASSERT_EQ(nodes.size(), kNodeCount);
    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        EXPECT_EQ(nodes[i].id, NodeId{i}) << "span must be in node-index order";
    }
}

// The leak check. Releasing everything must restore the starting state
// exactly; any asymmetry between allocate and release surfaces here and
// nowhere else.
TEST_F(ClusterTest, ReleaseRestoresTheClusterExactly) {
    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        cluster_.allocate(NodeId{i}, kPerNode);
    }
    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        cluster_.release(NodeId{i}, kPerNode);
    }

    EXPECT_EQ(cluster_.total_free(), cluster_.total_capacity());
    EXPECT_EQ(cluster_.node(NodeId{0}).running_jobs, 0u);
}

}  // namespace
}  // namespace atlas
