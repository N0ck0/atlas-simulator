// Resource accounting: allocate and release must agree, and free must never
// drift away from capacity.

#include "atlas/model/cluster.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

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

TEST_F(ClusterTest, FirstFitTakesTheLowestIndexAndMovesAlongAsNodesFill) {
    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        const std::optional<NodeId> placed = cluster_.first_fit(kPerNode);
        ASSERT_TRUE(placed.has_value()) << "no free node at iteration " << i;
        EXPECT_EQ(*placed, NodeId{i});

        cluster_.allocate(*placed, kPerNode);
        EXPECT_EQ(cluster_.node(*placed).running_jobs, 1u);
    }
}

TEST_F(ClusterTest, AFullClusterPlacesNothing) {
    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        cluster_.allocate(NodeId{i}, kPerNode);
    }

    EXPECT_FALSE(cluster_.first_fit(Resources{1u, 1u}).has_value());
    EXPECT_EQ(cluster_.total_free(), Resources{});
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
