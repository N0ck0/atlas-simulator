#pragma once

#include <cstddef>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/model/job.hpp"

namespace atlas {

struct TimeSample {
    std::size_t skipped;
    std::vector<Tick> times;
};

// Records the turnaround times of each job
// Includes amount of jobs that didn't finish in skipped, unfinished jobs don't appear returned vector
// Silently dropping skipped jobs would bias times downward without explanation
TimeSample turnaround_times(const std::vector<Job>& jobs);

// Records the queue wait times of each job
// Includes amount of jobs that didn't start in return struct
TimeSample queue_wait_times(const std::vector<Job>& jobs);

}  // namespace atlas
