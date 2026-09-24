#pragma once

#include <string>

#include "atlas/sched/scheduler.hpp"

namespace atlas {
class FirstFitScheduler : public Scheduler {
public:
    FirstFitScheduler() { name_ = "first_fit"; }
    FirstFitScheduler& operator=(const FirstFitScheduler& s) = delete;
    std::optional<NodeId> place(const Job& job, std::span<const Node> nodes) override;
    std::string_view name() const override;

private:
    std::string name_;
};
}  // namespace atlas