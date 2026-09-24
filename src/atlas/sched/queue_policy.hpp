#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/ids.hpp"
#include "atlas/model/node.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {

// What a waiting job exposes to a queue policy. Deliberately not a Job: the
// real `duration` is absent, so a policy physically cannot plan against the
// service time the simulator will actually use. The restriction is enforced by
// the type rather than by discipline, which is the only way it survives
// someone later adding a field in a hurry.
struct QueuedJob {
    JobId id{};
    Resources request{};
    Tick estimated_duration = 0;
    Tick submit_time = 0;
};

// What an in-flight job exposes. `estimated_finish` is start + the user's
// claim, not start + duration, for the same reason.
struct RunningJob {
    JobId id{};
    NodeId node{};
    Resources held{};
    Tick estimated_finish = 0;
};

// Which queued job runs next -- the other half of a scheduling decision, and
// the half that dominates: under strict FIFO the four placement policies
// separate by under half a percent, because the queue has already settled the
// outcome before any of them is consulted.
//
// Separate from Scheduler rather than folded into it. The two choices are
// independent, so one interface would multiply names instead of adding them,
// and would force every placement policy to carry a method it has no opinion
// about.
class QueuePolicy {
public:
    virtual ~QueuePolicy() = default;

    // Non-copyable for the same reason Scheduler is: these are held by
    // unique_ptr and used through the base, so a copy would slice.
    QueuePolicy(const QueuePolicy&) = delete;
    QueuePolicy& operator=(const QueuePolicy&) = delete;

    // Called only when `backlog[0]`, the head, could not be placed. Returns a
    // job from `backlog[1..]` that may jump it, or nullopt to leave everything
    // queued until something finishes.
    //
    // A returned job must fit on some node right now; the caller places it
    // immediately and has no recovery path if it does not. Check that with
    // Node::can_fit, not by asking the Scheduler: place() is a command rather
    // than a query, and RoundRobin advances a cursor, so a speculative call
    // would perturb the very policy this is meant to be orthogonal to.
    virtual std::optional<JobId> backfill(std::span<const QueuedJob> backlog,
                                          std::span<const Node> nodes,
                                          std::span<const RunningJob> running, Tick now) = 0;

    virtual std::string_view name() const = 0;

protected:
    QueuePolicy() = default;
};

// True when some node could take `request` right now.
inline bool fits_anywhere(const Resources& request, std::span<const Node> nodes) {
    for (const Node& n : nodes) {
        if (n.can_fit(request)) return true;
    }
    return false;
}

}  // namespace atlas
