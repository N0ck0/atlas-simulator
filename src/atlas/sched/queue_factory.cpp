#include "atlas/sched/queue_factory.hpp"

#include <array>

#include "atlas/sched/backfill.hpp"
#include "atlas/sched/fifo_queue.hpp"

namespace atlas {
namespace {

// fifo first, so it is the default and the baseline row of the comparison
// table without either of those facts being written down twice.
constexpr std::array<std::string_view, 2> kNames{"fifo", "backfill"};

}  // namespace

std::span<const std::string_view> queue_policy_names() { return kNames; }

std::unique_ptr<QueuePolicy> make_queue_policy(std::string_view name) {
    if (name == "fifo") return std::make_unique<FifoQueuePolicy>();
    if (name == "backfill") return std::make_unique<BackfillQueuePolicy>();
    return nullptr;
}

}  // namespace atlas
