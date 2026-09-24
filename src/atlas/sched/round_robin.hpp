#pragma once

#include <cstddef>

#include "atlas/sched/scheduler.hpp"

namespace atlas {
class RoundRobinScheduler : public Scheduler {
public:
    std::optional<NodeId> place(const Job& job, std::span<const Node> nodes) override;
    std::string_view name() const override;

private:
    std::size_t pos = 0;
};
}  // namespace atlas