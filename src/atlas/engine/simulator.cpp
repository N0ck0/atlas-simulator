#include "atlas/engine/simulator.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

#include "atlas/sched/fifo_queue.hpp"
#include "atlas/sched/first_fit.hpp"

namespace atlas {

Simulator::Simulator(Cluster cluster, std::vector<Job> jobs, TraceWriter* trace,
                     std::unique_ptr<Scheduler> scheduler,
                     std::unique_ptr<QueuePolicy> queue_policy)
    : cluster_(std::move(cluster)),
      jobs_(std::move(jobs)),
      trace_(trace),
      scheduler_(scheduler ? std::move(scheduler) : std::make_unique<FirstFitScheduler>()),
      queue_policy_(queue_policy ? std::move(queue_policy) : std::make_unique<FifoQueuePolicy>()) {}

LoadFactor Simulator::mean_utilization() const {
    assert(sim_end_ < kNever);
    return util_.mean(sim_end_, cluster_.total_capacity());
}

void Simulator::run() {
    for (const Job& j : jobs_) {
        queue_.schedule(j.submit_time, JobArrival{j.id});
        if (trace_ != nullptr) trace_->job_spec(j);
    }

    while (!queue_.empty()) {
        bool should_end = false;
        Event e = queue_.pop();
        assert(e.time >= now_);
        now_ = e.time;
        if (trace_ != nullptr) {
            trace_->write(e);
        }
        util_.advance(now_, cluster_.total_utilized());

        std::visit(
            [this, &should_end](auto& payload) {
                using T = std::decay_t<decltype(payload)>;

                if constexpr (std::is_same_v<T, JobArrival>) {
                    this->on_arrival(payload);
                } else if constexpr (std::is_same_v<T, JobFinish>) {
                    this->on_finish(payload);
                } else if constexpr (std::is_same_v<T, SimEnd>) {
                    this->on_sim_end(payload);
                    should_end = true;
                } else {
                    static_assert(kAlwaysFalse<T>, "Unhandled Event Type in Visitor");
                }
            },
            e.payload);

        if (should_end) {
            break;
        }

        if (queue_.empty()) {
            queue_.schedule(now_, SimEnd{});
        }
    }
}

bool Simulator::try_place(JobId id) {
    Job& j = job(id);
    const std::optional<NodeId> node = scheduler_->place(j, cluster_.nodes_span());
    if (!node.has_value()) return false;

    cluster_.allocate(*node, j.request);
    j.state = JobState::Running;
    j.start_time = now_;
    j.node = *node;
    running_.push_back(id);
    if (trace_ != nullptr) trace_->placement(now_, j.id, j.node);
    queue_.schedule(now_ + j.duration, JobFinish{j.id, j.node});
    return true;
}

void Simulator::rebuild_views() {
    backlog_view_.clear();
    for (const JobId id : pending_) {
        const Job& j = job(id);
        backlog_view_.push_back(QueuedJob{.id = j.id,
                                          .request = j.request,
                                          .estimated_duration = j.estimated_duration,
                                          .submit_time = j.submit_time});
    }

    running_view_.clear();
    for (const JobId id : running_) {
        const Job& j = job(id);
        running_view_.push_back(
            RunningJob{.id = j.id,
                       .node = j.node,
                       .held = j.request,
                       .estimated_finish = j.start_time + j.estimated_duration});
    }
}

void Simulator::drain() {
    // Phase 1: strict FIFO. Unchanged from before queue policies existed, and
    // the whole of the behaviour when the policy is fifo.
    while (!pending_.empty() && try_place(pending_.front())) {
        pending_.pop_front();
    }
    if (pending_.empty()) return;

    // Phase 2: the head is blocked. Ask what, if anything, may go around it.
    while (true) {
        rebuild_views();
        const std::optional<JobId> pick =
            queue_policy_->backfill(backlog_view_, cluster_.nodes_span(), running_view_, now_);
        if (!pick.has_value()) break;
        assert(*pick != pending_.front());

        // The policy contract is that a returned job fits somewhere now. If it
        // does not, the two interfaces disagree about what "fits" means and
        // the run is no longer measuring what it claims to.
        const bool placed = try_place(*pick);
        assert(placed);
        (void)placed;

        const auto it = std::find(pending_.begin(), pending_.end(), *pick);
        assert(it != pending_.end());
        pending_.erase(it);
        ++backfilled_;
    }
}

void Simulator::on_arrival(const JobArrival& arrival) {
    pending_.push_back(arrival.job);
    drain();
}

void Simulator::on_sim_end(const SimEnd& end) {
    util_.advance(now_, cluster_.total_utilized());
    sim_end_ = now_;
    (void)end;
}

void Simulator::on_finish(const JobFinish& finish) {
    Job& this_job = job(finish.job);
    assert(this_job.state == JobState::Running);
    assert(this_job.node == finish.node);
    cluster_.release(this_job.node, this_job.request);
    this_job.finish_time = now_;
    this_job.state = JobState::Done;

    // Removed before drain() so the queue policy never sees a finished job in
    // its running view and computes a reservation against resources that are
    // already free.
    const auto it = std::find(running_.begin(), running_.end(), finish.job);
    assert(it != running_.end());
    running_.erase(it);

    drain();
}

}  // namespace atlas
