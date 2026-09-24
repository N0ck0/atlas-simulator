#include "atlas/sched/backfill.hpp"

#include <algorithm>
#include <cstddef>

namespace atlas {

namespace {

bool fits(const Resources& free, const Resources& request) {
    return request.cores <= free.cores && request.memory_mb <= free.memory_mb;
}

}  // namespace

Tick shadow_time(const Resources& request, std::span<const Node> nodes,
                 std::span<const RunningJob> running, Tick now) {
    std::vector<RunningJob> on_node;
    Tick best = kNever;

    for (const Node& node : nodes) {
        if (node.can_fit(request)) return now;

        on_node.clear();
        for (const RunningJob& r : running) {
            if (r.node == node.id) on_node.push_back(r);
        }

        // Sorted by (estimated_finish, id). The id is not decoration: two jobs
        // can be predicted to end on the same tick, and an order that depended
        // on how they happened to sit in the running vector would make the
        // result a function of placement history rather than of the schedule.
        std::sort(on_node.begin(), on_node.end(), [](const RunningJob& a, const RunningJob& b) {
            if (a.estimated_finish != b.estimated_finish) {
                return a.estimated_finish < b.estimated_finish;
            }
            return a.id < b.id;
        });

        // Hand the node its resources back in the order they are predicted to
        // come free, and take the first moment the request fits.
        Resources free = node.free;
        for (const RunningJob& r : on_node) {
            free.cores += r.held.cores;
            free.memory_mb += r.held.memory_mb;
            if (fits(free, request)) {
                best = std::min(best, r.estimated_finish);
                break;
            }
        }
    }

    return best;
}

std::optional<JobId> BackfillQueuePolicy::backfill(std::span<const QueuedJob> backlog,
                                                   std::span<const Node> nodes,
                                                   std::span<const RunningJob> running, Tick now) {
    if (backlog.size() < 2) return std::nullopt;

    const Tick shadow = shadow_time(backlog[0].request, nodes, running, now);

    // kNever means no node could hold the head even empty, so there is no
    // reservation any candidate could violate and backfilling freely would be
    // safe. Refusing anyway is deliberate: a queue wedged behind an
    // unsatisfiable job surfaces as a large censored count beside a near-zero
    // utilization, which is how a node/profile capacity mismatch gets noticed
    // at all. That signal is worth more than the throughput it costs.
    if (shadow == kNever) return std::nullopt;

    for (std::size_t i = 1; i < backlog.size(); ++i) {
        const QueuedJob& candidate = backlog[i];

        // Predicted to be gone before the head needs the machine. Checked
        // first because it is arithmetic, where the fit check is a scan.
        if (now + candidate.estimated_duration > shadow) continue;
        if (!fits_anywhere(candidate.request, nodes)) continue;
        return candidate.id;
    }

    return std::nullopt;
}

std::string_view BackfillQueuePolicy::name() const { return "backfill"; }

}  // namespace atlas
