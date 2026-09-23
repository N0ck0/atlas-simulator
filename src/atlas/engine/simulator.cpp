#include "atlas/engine/simulator.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace atlas {

Simulator::Simulator(Cluster cluster, std::vector<Job> jobs, TraceWriter* trace)
    : cluster_(std::move(cluster)), jobs_(std::move(jobs)), trace_(trace) {}

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

void Simulator::drain() {
    while (!pending_.empty()) {
        JobId next_job_id = pending_.front();
        Job& next_job = job(next_job_id);
        auto res = cluster_.first_fit(next_job.request);
        if (!res.has_value()) break;

        //Job at front of pending queue can be assigned
        cluster_.allocate(res.value(), next_job.request);
        next_job.state = JobState::Running;
        next_job.start_time = now_;
        next_job.node = res.value();
        if (trace_ != nullptr) trace_->placement(now_, next_job.id, next_job.node);
        pending_.pop_front();
        queue_.schedule(now_ + next_job.duration, JobFinish{next_job.id, next_job.node});
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
    drain();
}

}  // namespace atlas
