#include "atlas/sched/fifo_queue.hpp"

namespace atlas {

// The head blocks and that is the whole policy. Written as a real class rather
// than a null pointer check in drain() so that "no backfill" is a choice the
// factory can name and the comparison table can put in a row.
std::optional<JobId> FifoQueuePolicy::backfill(std::span<const QueuedJob>, std::span<const Node>,
                                               std::span<const RunningJob>, Tick) {
    return std::nullopt;
}

std::string_view FifoQueuePolicy::name() const { return "fifo"; }

}  // namespace atlas
