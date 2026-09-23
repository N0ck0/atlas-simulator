#include "atlas/metrics/time_sample.hpp"

namespace atlas {

TimeSample turnaround_times(const std::vector<Job>& jobs) {
    std::vector<Tick> res;
    std::size_t skipped_jobs = 0;
    for (const Job& job : jobs) {
        if (job.state != JobState::Done) {
            skipped_jobs++;
            continue;
        }
        res.push_back(job.finish_time - job.submit_time);
    }
    return TimeSample{skipped_jobs, std::move(res)};
}

TimeSample queue_wait_times(const std::vector<Job>& jobs) {
    std::vector<Tick> res;
    std::size_t skipped_jobs = 0;
    for (const Job& job : jobs) {
        if (job.start_time == kNever) {
            skipped_jobs++;
            continue;
        }
        res.push_back(job.start_time - job.submit_time);
    }
    return TimeSample{skipped_jobs, std::move(res)};
}

}  // namespace atlas
