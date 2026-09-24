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

// A literal rather than a stored member: the name is a compile-time constant,
// and a string_view of a literal has static lifetime, so there is nothing to
// allocate and nothing that can outlive what it points at.
std::string_view FirstFitScheduler::name() const { return "first_fit"; }

}  // namespace atlas