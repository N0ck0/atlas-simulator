#pragma once

#include <cstddef>
#include <deque>
#include <memory>
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
#include "atlas/sched/queue_policy.hpp"
#include "atlas/sched/scheduler.hpp"

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
    Simulator(Cluster cluster, std::vector<Job> jobs, TraceWriter* trace = nullptr,
              std::unique_ptr<Scheduler> scheduler = nullptr,
              std::unique_ptr<QueuePolicy> queue_policy = nullptr);

    // Seeds the queue and runs it to exhaustion. Call once.
    void run();

    Tick now() const { return now_; }
    const std::vector<Job>& jobs() const { return jobs_; }
    const Cluster& cluster() const { return cluster_; }
    std::size_t pending_count() const { return pending_.size(); }

    // Jobs that started ahead of a blocked head. Zero under fifo by
    // construction, so it doubles as a check that a backfill run actually did
    // something rather than silently falling back.
    std::size_t backfilled_count() const { return backfilled_; }

    // Valid only after run() has returned.
    LoadFactor mean_utilization() const;

private:
    // Drains in two phases: strict FIFO from the head until something does not
    // fit, then whatever the queue policy lets jump the blocked head. The
    // second phase terminates because each iteration removes exactly one job
    // from pending_, which is finite -- there is no scan-to-exhaustion state
    // to track.
    void drain();

    // Places `id` if the scheduler finds it a node, and records everything
    // that follows from that. The one and only site that calls
    // Cluster::allocate, shared by both drain phases so a backfilled job and a
    // FIFO one cannot diverge in how they are started.
    bool try_place(JobId id);

    // Refreshes the spans handed to the queue policy. Rebuilt per attempt
    // because a placement changes what is free, and a stale reservation is
    // worse than no reservation.
    void rebuild_views();

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
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<QueuePolicy> queue_policy_;

    // Ids of jobs currently on a node. Kept alongside pending_ rather than
    // derived by scanning jobs_ for JobState::Running, which would be O(all
    // jobs) on every drain instead of O(jobs actually running).
    std::vector<JobId> running_;

    // Reused across calls so a drain does not allocate per attempt. Members
    // rather than locals purely for that; nothing reads them between calls.
    std::size_t backfilled_ = 0;
    std::vector<QueuedJob> backlog_view_;
    std::vector<RunningJob> running_view_;
};

}  // namespace atlas
