#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "atlas/sched/queue_policy.hpp"

namespace atlas {

// Mirrors scheduler_names()/make_scheduler, for the same reason and with the
// same fixed-table construction: a self-registering list would have an
// initialization order the standard does not pin down.
std::span<const std::string_view> queue_policy_names();

// The named policy, or nullptr if the name is not in queue_policy_names().
std::unique_ptr<QueuePolicy> make_queue_policy(std::string_view name);

}  // namespace atlas
