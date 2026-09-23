#include "atlas/metrics/offered_load.hpp"

#include <cassert>
#include <cstdint>

namespace atlas {

LoadFactor offered_load(const std::vector<Job>& jobs, const Cluster& cluster) {
    assert(jobs.size() > 0);
    Tick arrival_start = jobs[0].submit_time;
    Tick arrival_end = jobs[jobs.size() - 1].submit_time;
    Tick arrival_span = arrival_end - arrival_start;
    Resources cap = cluster.total_capacity();
    std::uint64_t free_cores = cap.cores;
    std::uint64_t free_memory_mb = cap.memory_mb;
    std::uint64_t core_seconds = free_cores * arrival_span;
    std::uint64_t memory_seconds = free_memory_mb * arrival_span;

    std::uint64_t job_cores_seconds = 0;
    std::uint64_t job_memory_seconds = 0;
    for (const Job& job : jobs) {
        job_cores_seconds += job.duration * job.request.cores;
        job_memory_seconds += job.duration * job.request.memory_mb;
    }
    double core_load = static_cast<double>(job_cores_seconds) / static_cast<double>(core_seconds);
    double memory_load =
        static_cast<double>(job_memory_seconds) / static_cast<double>(memory_seconds);

    return {core_load, memory_load};
}

}  // namespace atlas
