#include "atlas/sched/first_fit.hpp"

namespace atlas {

// returns the NodeId of the first Node in nodes that can accommodate the given job
// If no Nodes can fit the job, returns std::nullopt
std::optional<NodeId> FirstFitScheduler::place(const Job& job, std::span<const Node> nodes) {
    for (const Node& node : nodes) {
        if (node.can_fit(job.request)) {
            return node.id;
        }
    }
    return std::nullopt;
}

std::string_view FirstFitScheduler::name() const { return name_; }

}  // namespace atlas