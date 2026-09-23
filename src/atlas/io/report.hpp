#pragma once

#include "atlas/engine/simulator.hpp"
#include "atlas/io/options.hpp"

namespace atlas {

// One run's headline numbers: offered load beside achieved utilization, and the
// turnaround and queue-wait distributions. Seconds appear here and nowhere
// else; every other layer keeps integer ticks.
void report(const Simulator& sim, const Options& opts);

}  // namespace atlas
