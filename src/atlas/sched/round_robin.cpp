#include "atlas/sched/round_robin.hpp"

#include <span>

namespace atlas {
std::optional<NodeId> RoundRobinScheduler::place(const Job& job, std::span<const Node> nodes) {
    std::size_t start_pos = pos;
    std::size_t count = 0;
    while (count < nodes.size()) {
        count++;
        pos++;
        pos = pos % nodes.size();
        const Node& n = nodes[pos];
        if (n.can_fit(job.request)) {
            return n.id;
        }
        if (pos == start_pos) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

// A literal rather than a stored member: the name is a compile-time constant,
// and a string_view of a literal has static lifetime, so there is nothing to
// allocate and nothing that can outlive what it points at.
std::string_view RoundRobinScheduler::name() const { return "round_robin"; }
}  // namespace atlas