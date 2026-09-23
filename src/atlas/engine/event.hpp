#pragma once

#include <cstdint>
#include <variant>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/ids.hpp"

namespace atlas {

// Payloads carry IDs, never pointers into mutable state: an event can sit in
// the queue for a long stretch of simulated time before it fires.

struct JobArrival {
    JobId job;
};

struct JobFinish {
    JobId job;
    NodeId node;
};

struct SimEnd {};

// A variant rather than a class hierarchy. The set of event kinds is closed,
// so visitors can be checked for exhaustiveness at compile time, and value
// semantics keep events inline in the queue's vector with no heap allocation.
//
// Schedulers make the opposite choice deliberately: virtual dispatch, because
// that set is open with one operation over it, where this one is closed with
// many. See the Scheduler interface in S3.
using EventPayload = std::variant<JobArrival, JobFinish, SimEnd>;
template <typename...>
constexpr bool kAlwaysFalse = false;

// Indexed by EventPayload::index(). Keep in sync with the variant above.
constexpr const char* kEventNames[] = {"JobArrival", "JobFinish", "SimEnd"};

struct Event {
    Tick time;
    std::uint64_t seq;  // insertion order; breaks ties at equal time
    EventPayload payload;
};

}  // namespace atlas
