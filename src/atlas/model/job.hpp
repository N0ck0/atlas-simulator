#pragma once

#include <cstdint>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/ids.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {

enum class JobState : std::uint8_t { Queued, Running, Done };

// A unit of work. `request` is the receipt for its resources: allocate and
// release both read this one field, so they cannot disagree about how much was
// taken.
struct Job {
    JobId id{};
    Resources request{};
    Tick duration = 0;  // service time once it starts running
    Tick submit_time = 0;
    Tick start_time = kNever;
    Tick finish_time = kNever;
    NodeId node{};  // meaningful only once state != Queued
    JobState state = JobState::Queued;

    bool operator==(const Job&) const = default;
};

}  // namespace atlas
