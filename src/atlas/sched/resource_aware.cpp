#include "atlas/sched/resource_aware.hpp"

#include "atlas/sched/dominant_share.hpp"

namespace atlas {

std::optional<NodeId> ResourceAwareScheduler::place(const Job& job, std::span<const Node> nodes) {
    std::optional<NodeId> best;
    DominantShare best_share{};

    for (const Node& node : nodes) {
        if (!node.can_fit(job.request)) continue;

        // What the node would look like with this job on it. can_fit() has
        // already ruled out the case where either term exceeds capacity.
        const Resources after{
            node.capacity.cores - node.free.cores + job.request.cores,
            node.capacity.memory_mb - node.free.memory_mb + job.request.memory_mb};
        const DominantShare share = dominant_share(after, node.capacity);

        // Strictly greater, so ties leave `best` alone and the lowest node index
        // wins -- the same tie-break as every other scheduler.
        if (!best.has_value() || best_share < share) {
            best = node.id;
            best_share = share;
        }
    }

    return best;
}

std::string_view ResourceAwareScheduler::name() const { return "resource_aware"; }

}  // namespace atlas
