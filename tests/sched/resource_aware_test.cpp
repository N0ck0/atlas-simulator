// ResourceAware: multi-dimensional fit.
//
// LeastLoaded asks which node has the most room; a resource-aware policy asks
// about shape -- whether the job's core-to-memory ratio suits what a node has
// left. Best-fit (smallest adequate remainder) and dominant-resource fit (rank
// on the dimension the job stresses most) are two well-known answers and they
// disagree. This one packs onto the tightest fit, which makes it the deliberate
// opposite of LeastLoaded; the comparison between them is only meaningful
// because they differ for a statable reason.

#include "atlas/sched/resource_aware.hpp"

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

class ResourceAwareTest : public ::testing::Test {
protected:
    Cluster cluster_{kNodeCount, kPerNode};
    ResourceAwareScheduler scheduler_;
};

// Whatever the ranking, an exact fit is always placeable: a node with precisely
// the requested amount free must not be rejected for having no slack left.
TEST_F(ResourceAwareTest, AcceptsAnExactFit) {
    Cluster single{1u, kPerNode};

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(kPerNode), single.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{0});
}

// A node can be unplaceable on one dimension while the other has room to spare.
// A policy that collapses both dimensions into one number before checking
// feasibility is liable to get this wrong.
TEST_F(ResourceAwareTest, RespectsEachDimensionIndependently) {
    Cluster single{1u, kPerNode};
    single.allocate(NodeId{0}, Resources{0u, 15'000u});  // memory nearly gone, all cores free

    EXPECT_FALSE(
        scheduler_.place(job_requesting(Resources{1u, 8'192u}), single.nodes_span()).has_value())
        << "cores are free, but the memory is not there";
    EXPECT_TRUE(
        scheduler_.place(job_requesting(Resources{8u, 1'024u}), single.nodes_span()).has_value())
        << "memory is tight, but this job barely needs any";
}

TEST_F(ResourceAwareTest, BreaksTiesByLowestNodeIndex) {
    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{0}) << "all four nodes are identical here";
}

TEST_F(ResourceAwareTest, IdentifiesItselfAsResourceAware) {
    EXPECT_EQ(scheduler_.name(), "resource_aware");
}

// The policy, and the case that separates it from LeastLoaded. Node 0 is empty
// and node 1 is half used; both fit the job.
//
// LeastLoaded spreads, so it would take the empty node 0. This packs: it takes
// node 1, the tightest fit, leaving node 0 whole for a job that needs a whole
// node. Two policies, opposite answers, same input -- which is what makes them
// worth comparing in a table.
TEST_F(ResourceAwareTest, PacksOntoTheTightestFitRatherThanSpreading) {
    cluster_.allocate(NodeId{1}, Resources{4u, 8'192u});

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{1}) << "should consume the busier node, not the empty one";
}

// Packing is bounded by feasibility: the tightest fit among nodes that fit, not
// the tightest fit overall. Node 3 is fullest but cannot take the job.
TEST_F(ResourceAwareTest, NeverPacksOntoANodeThatDoesNotFit) {
    cluster_.allocate(NodeId{3}, Resources{7u, 15'000u});

    const Job job = job_requesting(Resources{2u, 2'048u});
    const std::optional<NodeId> placed = scheduler_.place(job, cluster_.nodes_span());

    ASSERT_TRUE(placed.has_value());
    EXPECT_NE(*placed, NodeId{3});
    EXPECT_TRUE(cluster_.node(*placed).can_fit(job.request));
}

}  // namespace
}  // namespace atlas
