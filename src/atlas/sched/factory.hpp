#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "atlas/sched/scheduler.hpp"

namespace atlas {

// Every scheduler name the CLI accepts, in a fixed order. One table drives the
// factory, the argument validation, and the usage text, so a scheduler cannot
// be constructible but unlisted, or listed but unconstructible.
std::span<const std::string_view> scheduler_names();

// The named scheduler, or nullptr if the name is not in scheduler_names().
// Returning null rather than throwing keeps the error where the bad input came
// from: parse_args rejects it and prints usage, which is what a user of a CLI
// wants to see.
std::unique_ptr<Scheduler> make_scheduler(std::string_view name);

}  // namespace atlas
