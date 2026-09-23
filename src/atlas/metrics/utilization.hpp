#pragma once

#include <cstdint>

#include "atlas/engine/clock.hpp"
#include "atlas/metrics/load_factor.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {

// Time-weighted integral of resources in use, Advanced at
// every event, never sampled.
//
// Events are not evenly spaced, so averaging samples taken at event times would
// weight a microsecond as heavily as a minute. advance() must be called before
// the handler changes state, so the interval is credited to the resources that
// were actually held during it.
struct UtilizationIntegral {
private:
    Tick last_checkin_ = 0;
    std::uint64_t core_ticks_ = 0;
    //potential overflow risk? maybe convert to gb or regular seconds?
    std::uint64_t memory_ticks_ = 0;

public:
    // Advances the integral to `now`, crediting the interval since the last
    // advance to `in_use`.
    // Precondition: now >= the last time advanced.
    void advance(Tick now, const Resources& in_use);

    // Mean fraction of `capacity` in use over [0, end], per dimension.
    LoadFactor mean(Tick end, const Resources& capacity) const;
};

}  // namespace atlas
