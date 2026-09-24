// LeastLoaded: place on the node with the most room.
//
// Cores and megabytes are not comparable, so "most room" needs a rule for
// collapsing two dimensions into one ranking. The minimum free fraction across
// dimensions, the mean, and cores-with-memory-as-tie-break are all defensible
// and each produces a different comparison table. This one ranks on the
// scarcer dimension; the cases below pin that choice.

#include "atlas/sched/least_loaded.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "atlas/engine/ids.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {
namespace {

constexpr Resources kPerNode{8u, 16'384u};
constexpr std::uint32_t kNodeCount = 4u;

Job job_requesting(Resources request) {
    Job j{};
    j.request = request;
    return j;
}

class LeastLoadedTest : public ::testing::Test {
protected:
    Cluster cluster_{kNodeCount, kPerNode};
    LeastLoadedScheduler scheduler_;
};

// Unambiguous: node 3 is emptier on both dimensions than every other node, so
// no sensible combining rule can prefer another.
TEST_F(LeastLoadedTest, PrefersTheNodeWithMoreRoomOnEveryDimension) {
    cluster_.allocate(NodeId{0}, Resources{6u, 12'288u});
    cluster_.allocate(NodeId{1}, Resources{4u, 8'192u});
    cluster_.allocate(NodeId{2}, Resources{2u, 4'096u});
    // node 3 untouched

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{3});
}

// This is what separates it from FirstFit, which would answer node 0.
TEST_F(LeastLoadedTest, DoesNotSimplyTakeTheLowestIndexThatFits) {
    cluster_.allocate(NodeId{0}, Resources{4u, 8'192u});

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_NE(*placed, NodeId{0});
}

// Emptiest is only a preference among nodes that can actually hold the job. Here
// the emptiest node is too small on memory, so a fuller-but-adequate node wins.
TEST_F(LeastLoadedTest, OnlyRanksNodesThatFit) {
    Cluster mixed{2u, kPerNode};
    mixed.allocate(NodeId{0}, Resources{1u, 16'000u});  // nearly all memory gone, cores free
    mixed.allocate(NodeId{1}, Resources{5u, 2'048u});   // fuller overall, memory to spare

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 8'192u}), mixed.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{1});
}

// Identical nodes must not be separated by anything the standard leaves
// unspecified. Lowest index matches FirstFit; any fixed total order would do,
// but the choice has to be pinned somewhere.
TEST_F(LeastLoadedTest, BreaksTiesByLowestNodeIndex) {
    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{0}) << "all four nodes are identical here";
}

// The ranking rule, pinned by the two cases that distinguish it from the
// obvious alternatives. Node capacity here is 8 cores / 16384 MB.
//
// Against ranking on the MEAN of the two fractions: node 0 is 75% on cores and
// 0% on memory (mean 0.375); node 1 is 50% on both (mean 0.5). A mean would
// prefer node 0. Taking the larger fraction prefers node 1, because node 0 is
// three quarters gone on the dimension that actually constrains it.
TEST_F(LeastLoadedTest, RanksOnTheLargerFractionNotTheAverage) {
    cluster_.allocate(NodeId{0}, Resources{6u, 0u});
    cluster_.allocate(NodeId{1}, Resources{4u, 8'192u});
    cluster_.allocate(NodeId{2}, kPerNode);
    cluster_.allocate(NodeId{3}, kPerNode);

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{1});
}

// Against ranking on free cores alone: node 0 has the most free cores (7 of 8)
// but almost no memory left, so it is the more constrained node despite looking
// emptiest on one axis. A single-dimension rule picks node 0; this one picks
// node 1.
TEST_F(LeastLoadedTest, ACoreRichNodeWithNoMemoryIsNotTheLeastLoaded) {
    cluster_.allocate(NodeId{0}, Resources{1u, 15'000u});
    cluster_.allocate(NodeId{1}, Resources{4u, 4'096u});
    cluster_.allocate(NodeId{2}, kPerNode);
    cluster_.allocate(NodeId{3}, kPerNode);

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{1});
}

TEST_F(LeastLoadedTest, IdentifiesItselfAsLeastLoaded) {
    EXPECT_EQ(scheduler_.name(), "least_loaded");
}

}  // namespace
}  // namespace atlas
