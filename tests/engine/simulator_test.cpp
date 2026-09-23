// The event loop: every job completes, resources are conserved, and the same
// seed replays the same history.

#include "atlas/engine/simulator.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/resources.hpp"
#include "atlas/model/workload.hpp"

namespace atlas {
namespace {

constexpr std::uint64_t kSeed = 7;
constexpr std::uint32_t kCount = 2'000;
constexpr double kRate = 0.025;
constexpr Resources kPerNode{16u, 65'536u};

std::vector<Job> make_workload() {
    WorkloadGenerator gen(kSeed, kRate);
    return gen.generate(kCount);
}

// The default operating point: rho ~0.67, so the backlog path is genuinely
// exercised rather than passed over.
class SimulatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        workload_ = make_workload();
        sim_ = std::make_unique<Simulator>(Cluster(8u, kPerNode), workload_);
        sim_->run();
    }

    std::vector<Job> workload_;
    std::unique_ptr<Simulator> sim_;
};

TEST_F(SimulatorTest, KeepsEveryJob) { EXPECT_EQ(sim_->jobs().size(), kCount); }

TEST_F(SimulatorTest, EveryJobEndsDoneWithCausalTimestamps) {
    const std::vector<Job>& done = sim_->jobs();
    ASSERT_EQ(done.size(), kCount);

    for (std::size_t i = 0; i < done.size(); ++i) {
        const Job& j = done[i];
        SCOPED_TRACE(testing::Message() << "job index " << i);

        EXPECT_EQ(j.state, JobState::Done);
        ASSERT_NE(j.start_time, kNever);
        ASSERT_NE(j.finish_time, kNever);
        EXPECT_GE(j.start_time, j.submit_time) << "started before it was submitted";
        EXPECT_EQ(j.finish_time - j.start_time, j.duration) << "ran for the wrong duration";
    }
}

// The simulator must not rewrite the workload it was handed: a job's request,
// duration and submit time are inputs, not state.
TEST_F(SimulatorTest, PreservesTheWorkloadItWasGiven) {
    const std::vector<Job>& done = sim_->jobs();
    ASSERT_EQ(done.size(), workload_.size());

    for (std::size_t i = 0; i < done.size(); ++i) {
        SCOPED_TRACE(testing::Message() << "job index " << i);
        EXPECT_EQ(done[i].request, workload_[i].request);
        EXPECT_EQ(done[i].duration, workload_[i].duration);
        EXPECT_EQ(done[i].submit_time, workload_[i].submit_time);
    }
}

// The leak check, same idea as the cluster one: if allocate and release ever
// disagree, or a job is placed twice, free no longer returns to capacity.
TEST_F(SimulatorTest, ConservesResourcesAndDrainsTheBacklog) {
    EXPECT_EQ(sim_->pending_count(), 0u);
    EXPECT_EQ(sim_->cluster().total_free(), sim_->cluster().total_capacity());
}

TEST_F(SimulatorTest, ClockEndsAtOrAfterTheLastFinish) {
    Tick last_finish = 0;
    for (const Job& j : sim_->jobs()) {
        if (j.finish_time != kNever && j.finish_time > last_finish) {
            last_finish = j.finish_time;
        }
    }
    EXPECT_GE(sim_->now(), last_finish);
}

TEST_F(SimulatorTest, SameSeedReplaysAnIdenticalHistory) {
    Simulator again(Cluster(8u, kPerNode), workload_);
    again.run();

    EXPECT_EQ(again.jobs(), sim_->jobs());
}

// Without this, every check above could pass on a cluster that never queued
// anything, leaving the backlog path dead code.
TEST_F(SimulatorTest, SomeJobActuallyWaitsAtTheDefaultLoad) {
    Tick queued_time = 0;
    for (const Job& j : sim_->jobs()) {
        if (j.start_time != kNever) queued_time += j.start_time - j.submit_time;
    }
    EXPECT_GT(queued_time, 0u);
}

// Separates "the loop works" from "the loop happens to be backlogged": with a
// node per job, every job must start the instant it arrives.
TEST(SimulatorCapacityExtremes, AnOversizedClusterStartsEveryJobOnArrival) {
    const std::vector<Job> workload = make_workload();
    Simulator roomy(Cluster(kCount, kPerNode), workload);
    roomy.run();

    for (const Job& j : roomy.jobs()) {
        ASSERT_EQ(j.start_time, j.submit_time);
    }
}

// A cluster too small for any job must place nothing -- and must terminate
// rather than spin on a backlog it can never drain.
TEST(SimulatorCapacityExtremes, AClusterThatFitsNothingPlacesNothingAndStillTerminates) {
    const std::vector<Job> workload = make_workload();
    Simulator tiny(Cluster(1u, Resources{1u, 1'024u}), workload);
    tiny.run();

    for (const Job& j : tiny.jobs()) {
        ASSERT_EQ(j.start_time, kNever);
    }
    EXPECT_EQ(tiny.pending_count(), kCount);
}

}  // namespace
}  // namespace atlas
