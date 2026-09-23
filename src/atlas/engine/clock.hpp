#pragma once

#include <cstdint>
#include <limits>

namespace atlas {

// Simulated time in microseconds. Integer rather than floating point so that
// arithmetic stays exact and results are identical across optimization levels.
// Floating point would accumulate drift, make equality unreliable, and vary with
// the optimizer -- any one of which silently invalidates an experimental result.
// Seconds exist only in the human-readable report.
using Tick = std::uint64_t;

constexpr Tick kMicrosecond = 1;
constexpr Tick kMillisecond = 1'000;
constexpr Tick kSecond = 1'000'000;

// A Tick that has not been set. Distinguishes "not started" from t=0.
constexpr Tick kNever = std::numeric_limits<Tick>::max();

}  // namespace atlas
