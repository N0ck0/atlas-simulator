#pragma once

#include <vector>

#include "atlas/sched/queue_policy.hpp"

namespace atlas {

// The earliest time `request` could be placed, assuming every running job ends
// at its estimated finish and nothing else is scheduled. kNever when no node
// could ever hold it even when empty.
//
// Exposed rather than kept private to backfill.cpp because it is the part with
// hand-computable answers, and so the part worth testing directly.
Tick shadow_time(const Resources& request, std::span<const Node> nodes,
                 std::span<const RunningJob> running, Tick now);

// EASY backfill: a job further down the queue may start ahead of a blocked
// head, provided `now + estimated_duration <= shadow_time(head)` -- it is
// predicted to be gone before the head's reservation comes due.
//
// That test is what separates this from "run the first job that fits", which
// starves a 16-core head under a steady trickle of small jobs.
//
// The guarantee is against the reservation, not against FIFO. Estimates
// over-claim, so the shadow time is later than the head could truly have
// started, and a job admitted under that slack may still hold resources at the
// earlier true moment. The head starts no later than its reservation, but can
// start later than it would have without backfill.
class BackfillQueuePolicy final : public QueuePolicy {
public:
    std::optional<JobId> backfill(std::span<const QueuedJob> backlog, std::span<const Node> nodes,
                                  std::span<const RunningJob> running, Tick now) override;
    std::string_view name() const override;

private:
    // Scratch for the per-node finish ordering, reused across calls so that a
    // drain does not allocate once per node per placement.
    std::vector<RunningJob> on_node_;
};

}  // namespace atlas
