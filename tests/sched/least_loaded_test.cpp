// LeastLoaded: place on the node with the most room.
//
// TODO(nick): implement src/atlas/sched/least_loaded.{hpp,cpp}, add
// "least_loaded" to the factory's table and kNames, add the .cpp to atlas_lib,
// and uncomment this file in tests/CMakeLists.txt.
//
// One design decision is yours and is deliberately not pinned down below: cores
// and megabytes are not comparable, so "most room" needs a rule for combining
// two dimensions into one ranking. Fraction of capacity free per dimension and
// then the minimum? The mean? Cores first, memory as tie-break? Each is
// defensible and each produces a different table in 3.4, so whichever you pick,
// write the case that distinguishes it into TwoDimensionalRankingIsYourChoice
// below. The cases above it hold whatever you decide.

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

// Invariant 4: identical nodes must not be separated by anything the standard
// leaves unspecified. Lowest index is the recommended rule -- it matches
// FirstFit and needs no justification in a write-up -- but any fixed, total
// order works. If you choose differently, change the expectation here.
TEST_F(LeastLoadedTest, BreaksTiesByLowestNodeIndex) {
    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{0}) << "all four nodes are identical here";
}

// Yours to write once you have chosen how to combine the two dimensions. Set up
// a cluster where one node has more free cores and another has more free
// memory, then assert the answer your rule gives. Remove the DISABLED_ prefix
// when you do -- GoogleTest reports disabled tests as skipped, so it will keep
// reminding you until then.
TEST_F(LeastLoadedTest, DISABLED_TwoDimensionalRankingIsYourChoice) {
    FAIL() << "decide how cores and memory combine, then encode it here";
}

TEST_F(LeastLoadedTest, IdentifiesItselfAsLeastLoaded) {
    EXPECT_EQ(scheduler_.name(), "least_loaded");
}

}  // namespace
}  // namespace atlas
