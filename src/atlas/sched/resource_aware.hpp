#pragma once

#include "atlas/sched/scheduler.hpp"

namespace atlas {

// Packs rather than spreads: among the nodes that fit, places on the one left
// most utilized afterwards -- maximize over nodes of max(core_util,
// memory_util) measured *after* the job lands.
//
// This is best-fit bin packing generalized to two dimensions, and it is the
// deliberate opposite of LeastLoaded. Spreading keeps every node partly full,
// which is fine until a `large` job needs a whole node and no node has one
// free; packing consumes the already-busy nodes first and keeps empty ones
// empty for the jobs that need them. Unlike LeastLoaded it reads the job's
// shape, not just the node's state: a memory-heavy job and a core-heavy job of
// the same size rank nodes differently.
class ResourceAwareScheduler : public Scheduler {
public:
    std::optional<NodeId> place(const Job& job, std::span<const Node> nodes) override;
    std::string_view name() const override;
};

}  // namespace atlas
