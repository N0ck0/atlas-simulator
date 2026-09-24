// Placement policy. Cluster does resource accounting only; choosing a node is
// a scheduler's job.
//
// These cases pin the behaviour the golden trace was recorded against, so a
// FirstFitScheduler that scanned in a different order or broke ties differently
// would fail here as well as there.

#include "atlas/sched/first_fit.hpp"

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

class FirstFitTest : public ::testing::Test {
protected:
    Cluster cluster_{kNodeCount, kPerNode};
    FirstFitScheduler scheduler_;
};

TEST_F(FirstFitTest, TakesTheLowestIndexAndMovesAlongAsNodesFill) {
    const Job whole_node = job_requesting(kPerNode);

    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        const std::optional<NodeId> placed = scheduler_.place(whole_node, cluster_.nodes_span());
        ASSERT_TRUE(placed.has_value()) << "no free node at iteration " << i;
        EXPECT_EQ(*placed, NodeId{i}) << "first fit must take the lowest free index";

        cluster_.allocate(*placed, whole_node.request);
    }
}

TEST_F(FirstFitTest, ReturnsNulloptWhenNoNodeFits) {
    for (std::uint32_t i = 0; i < kNodeCount; ++i) {
        cluster_.allocate(NodeId{i}, kPerNode);
    }

    EXPECT_FALSE(
        scheduler_.place(job_requesting(Resources{1u, 1u}), cluster_.nodes_span()).has_value());
}

// A job can be unplaceable on one dimension while the other has room to spare,
// which a single combined size comparison would miss.
TEST_F(FirstFitTest, RespectsEachDimensionIndependently) {
    EXPECT_FALSE(
        scheduler_.place(job_requesting(Resources{9u, 1'024u}), cluster_.nodes_span()).has_value())
        << "too many cores, memory to spare";
    EXPECT_FALSE(
        scheduler_.place(job_requesting(Resources{1u, 20'000u}), cluster_.nodes_span()).has_value())
        << "too much memory, cores to spare";
}

TEST_F(FirstFitTest, SkipsAFullNodeForOneWithRoom) {
    cluster_.allocate(NodeId{0}, kPerNode);

    const std::optional<NodeId> placed =
        scheduler_.place(job_requesting(Resources{1u, 1'024u}), cluster_.nodes_span());
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(*placed, NodeId{1});
}

// The factory selects a scheduler by this string, so it is part of the
// contract rather than a label.
TEST_F(FirstFitTest, IdentifiesItselfAsFirstFit) { EXPECT_EQ(scheduler_.name(), "first_fit"); }

}  // namespace
}  // namespace atlas
