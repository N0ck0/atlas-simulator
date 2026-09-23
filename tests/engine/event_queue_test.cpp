// Invariant 2: events come out ordered by (time, seq), and the queue owns the
// counter so no caller can forget to stamp one.

#include "atlas/engine/event_queue.hpp"

#include <gtest/gtest.h>

namespace atlas {
namespace {

// Time ordering pulls seq 1 behind the three events at t=1s, which the
// tie-break then holds in scheduling order.
const std::uint64_t expected_seq[] = {0, 2, 3, 4, 1, 5};
constexpr std::size_t kExpectedCount = 6;

TEST(EventQueue, SizeEqualsEventsScheduled) {
    EventQueue q;

    // Scheduled out of order, with a three-way tie at t=1s. Trailing comments
    // are the sequence number each call consumes.
    q.schedule(500 * kMillisecond, JobArrival{JobId{1}});     // seq 0
    q.schedule(2 * kSecond, JobFinish{JobId{1}, NodeId{0}});  // seq 1
    q.schedule(1 * kSecond, JobArrival{JobId{2}});            // seq 2
    q.schedule(1 * kSecond, JobArrival{JobId{3}});            // seq 3
    q.schedule(1 * kSecond, JobArrival{JobId{4}});            // seq 4
    q.schedule(10 * kSecond, SimEnd{});
    EXPECT_EQ(q.size(), kExpectedCount);
}

TEST(EventQueue, HasClockNonDecreasingAndSequenceIncreasing) {
    EventQueue q;

    // Scheduled out of order, with a three-way tie at t=1s. Trailing comments
    // are the sequence number each call consumes.
    q.schedule(500 * kMillisecond, JobArrival{JobId{1}});     // seq 0
    q.schedule(2 * kSecond, JobFinish{JobId{1}, NodeId{0}});  // seq 1
    q.schedule(1 * kSecond, JobArrival{JobId{2}});            // seq 2
    q.schedule(1 * kSecond, JobArrival{JobId{3}});            // seq 3
    q.schedule(1 * kSecond, JobArrival{JobId{4}});            // seq 4
    q.schedule(10 * kSecond, SimEnd{});
    Tick clock = 0;
    for (std::size_t i = 0; i < kExpectedCount; ++i) {
        const Event e = q.pop();

        EXPECT_EQ(e.seq, expected_seq[i]);
        EXPECT_GE(e.time, clock);
        clock = e.time;
    }
}

TEST(EventQueue, IsEmptyAfterPopping) {
    EventQueue q;

    // Scheduled out of order, with a three-way tie at t=1s. Trailing comments
    // are the sequence number each call consumes.
    q.schedule(500 * kMillisecond, JobArrival{JobId{1}});     // seq 0
    q.schedule(2 * kSecond, JobFinish{JobId{1}, NodeId{0}});  // seq 1
    q.schedule(1 * kSecond, JobArrival{JobId{2}});            // seq 2
    q.schedule(1 * kSecond, JobArrival{JobId{3}});            // seq 3
    q.schedule(1 * kSecond, JobArrival{JobId{4}});            // seq 4
    q.schedule(10 * kSecond, SimEnd{});
    Tick clock = 0;
    for (std::size_t i = 0; i < kExpectedCount; ++i) {
        const Event e = q.pop();
    }
    EXPECT_TRUE(q.empty());
}
}  // namespace

}  // namespace atlas
