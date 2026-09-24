#include "atlas/sched/least_loaded.hpp"

#include "atlas/sched/dominant_share.hpp"

namespace atlas {

std::optional<NodeId> LeastLoadedScheduler::place(const Job& job, std::span<const Node> nodes) {
    std::optional<NodeId> best;
    DominantShare best_share{};

    for (const Node& node : nodes) {
        if (!node.can_fit(job.request)) continue;

        const Resources used{node.capacity.cores - node.free.cores,
                             node.capacity.memory_mb - node.free.memory_mb};
        const DominantShare share = dominant_share(used, node.capacity);

        // Strictly less, so equal scores leave `best` alone and the lowest node
        // index wins. An unbroken tie would be nondeterminism, not a detail.
        if (!best.has_value() || share < best_share) {
            best = node.id;
            best_share = share;
        }
    }

    return best;
}

std::string_view LeastLoadedScheduler::name() const { return "least_loaded"; }

}  // namespace atlas
