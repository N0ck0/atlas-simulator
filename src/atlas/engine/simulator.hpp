#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/event.hpp"
#include "atlas/engine/event_queue.hpp"
#include "atlas/engine/ids.hpp"
#include "atlas/io/trace.hpp"
#include "atlas/metrics/load_factor.hpp"
#include "atlas/metrics/utilization.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"

namespace atlas {

// Drives the clock. Owns the jobs and the backlog; Cluster still never sees a
// Job, only a Resources request.
//
// pending_ holds ids rather than pointers because jobs_ is a vector: a Job*
// parked in the backlog would dangle the moment it reallocated.
class Simulator {
public:
    // `jobs` must have dense ids matching its indices, as WorkloadGenerator
    // produces: jobs_[5] is JobId{5}.
    Simulator(Cluster cluster, std::vector<Job> jobs, TraceWriter* trace = nullptr);

    // Seeds the queue and runs it to exhaustion. Call once.
    void run();

    Tick now() const { return now_; }
    const std::vector<Job>& jobs() const { return jobs_; }
    const Cluster& cluster() const { return cluster_; }
    std::size_t pending_count() const { return pending_.size(); }

    // Valid only after run() has returned.
    LoadFactor mean_utilization() const;

private:
    // The one and only placement site. Nothing else calls Cluster::allocate.
    void drain();

    void on_arrival(const JobArrival& arrival);
    void on_finish(const JobFinish& finish);
    void on_sim_end(const SimEnd& end);

    Job& job(JobId id) { return jobs_[static_cast<std::uint32_t>(id)]; }

    Cluster cluster_;
    std::vector<Job> jobs_;
    std::deque<JobId> pending_;
    EventQueue queue_;
    Tick now_ = 0;
    UtilizationIntegral util_;
    Tick sim_end_ = kNever;
    TraceWriter* trace_ = nullptr;
};

}  // namespace atlas
