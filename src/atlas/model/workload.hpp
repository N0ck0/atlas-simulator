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
    // `estimate_padding` is the widest over-claim a user makes: walltime
    // estimates are drawn uniformly from [1, 1 + padding] times the true
    // duration. Zero means perfect estimates, which is a useful control and a
    // dishonest default -- real estimates are padded and erratically so.
    WorkloadGenerator(std::uint64_t seed, double arrival_rate_per_second,
                      double estimate_padding = kDefaultEstimatePadding)
        : rng_(seed), arrival_rate_(arrival_rate_per_second), estimate_padding_(estimate_padding) {
        assert(arrival_rate_per_second > 0.0);
        assert(estimate_padding >= 0.0);
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

        // Estimates are drawn in a second pass rather than inside the loop
        // above. Both orders are deterministic, but interleaving would shift
        // every subsequent draw and silently change the arrival times and
        // durations of an existing seed -- which would make every measurement
        // taken before this feature incomparable with every one after it.
        for (Job& j : jobs) {
            const double padding = 1.0 + estimate_padding_ * rng_.next_uniform();
            j.estimated_duration = static_cast<Tick>(static_cast<double>(j.duration) * padding);
        }

        return jobs;
    }

private:
    RandomSource rng_;
    double arrival_rate_;
    double estimate_padding_;
};

}  // namespace atlas
