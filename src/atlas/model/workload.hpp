#pragma once

#include <cassert>
#include <cstdint>
#include <iterator>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/ids.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/profiles.hpp"
#include "atlas/model/random.hpp"

namespace atlas {

// Builds the whole workload before the clock starts, so the job sequence is a
// function of the seed alone and not of any scheduling decision.
class WorkloadGenerator {
public:
    WorkloadGenerator(std::uint64_t seed, double arrival_rate_per_second)
        : rng_(seed), arrival_rate_(arrival_rate_per_second) {
        assert(arrival_rate_per_second > 0.0);
    }

    // `count` jobs with dense ids 0..count-1 and non-decreasing submit_time.
    std::vector<Job> generate(std::uint32_t count) {
        std::vector<Job> jobs;
        jobs.reserve(count);
        Tick cur_tick{0};

        for (std::uint32_t num = 0; num < count; num++) {
            const JobProfile& profile = kJobProfiles[rng_.next_below(std::size(kJobProfiles))];
            const Tick duration = rng_.next_exponential(static_cast<double>(profile.mean_duration));
            cur_tick += rng_.next_interarrival(arrival_rate_);
            Job j{.id = JobId{num},
                  .request = profile.request,
                  .duration = duration,
                  .submit_time = cur_tick};
            jobs.push_back(j);
        }

        return jobs;
    }

private:
    RandomSource rng_;
    double arrival_rate_;
};

}  // namespace atlas
