#pragma once

#include "atlas/sched/queue_policy.hpp"

namespace atlas {

// Strict FIFO: nothing ever jumps the head. The baseline every backfill result
// is measured against, and the policy that reproduces the behaviour the
// simulator had before queue policies existed -- byte for byte, which is what
// the golden trace checks.
class FifoQueuePolicy final : public QueuePolicy {
public:
    std::optional<JobId> backfill(std::span<const QueuedJob> backlog, std::span<const Node> nodes,
                                  std::span<const RunningJob> running, Tick now) override;
    std::string_view name() const override;
};

}  // namespace atlas
