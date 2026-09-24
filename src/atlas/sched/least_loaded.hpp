#pragma once

#include "atlas/sched/scheduler.hpp"

namespace atlas {

// Spreads load: places on the node whose most constrained dimension is least
// constrained, which is minimize over nodes of max(core_util, memory_util).
//
// Utilization as a fraction of each node's own capacity is what makes cores and
// megabytes comparable, and taking the larger of the two is what stops a node
// with idle cores but no memory left from looking empty. Ranking on the most
// constrained resource this way is the dominant-resource idea from DRF applied
// to nodes rather than to tenants.
class LeastLoadedScheduler : public Scheduler {
public:
    std::optional<NodeId> place(const Job& job, std::span<const Node> nodes) override;
    std::string_view name() const override;
};

}  // namespace atlas
