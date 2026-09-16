// Atlas: discrete-event simulator for cluster scheduling policies.
//
// Stage 1 lives in one translation unit; CMake and real headers arrive in S2.
// Step 1.1: simulated time, event types, and the event queue.
//
// Build:
//   g++ -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2
//       -o atlas src/atlas.cpp && ./atlas

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <variant>
#include <vector>
#include <cassert>

// Simulated time in microseconds. Integer rather than floating point so that
// arithmetic stays exact and results are identical across optimization levels.
using Tick = std::uint64_t;

constexpr Tick kMicrosecond = 1;
constexpr Tick kMillisecond = 1'000;
constexpr Tick kSecond      = 1'000'000;

// Distinct types, layout-identical to uint32_t, so a JobId cannot be passed
// where a NodeId is expected. Construct with JobId{7}.
enum class JobId : std::uint32_t {};
enum class NodeId : std::uint32_t {};

// Payloads carry IDs, never pointers into mutable state: an event can sit in
// the queue for a long stretch of simulated time before it fires.

struct JobArrival {
    JobId job;
};

struct JobFinish {
    JobId job;
    NodeId node;
};

struct SimEnd {};

// A variant rather than a class hierarchy. The set of event kinds is closed,
// so visitors can be checked for exhaustiveness at compile time, and value
// semantics keep events inline in the queue's vector with no heap allocation.
using EventPayload = std::variant<JobArrival, JobFinish, SimEnd>;

// Indexed by EventPayload::index(). Keep in sync with the variant above.
constexpr const char* kEventNames[] = {"JobArrival", "JobFinish", "SimEnd"};

struct Event {
    Tick time;
    std::uint64_t seq;  // insertion order; breaks ties at equal time
    EventPayload payload;
};

// Earliest time first, then insertion order. std::priority_queue is a max-heap
// whose comparator means "a is less important than b", so both tests are
// inverted. Strict weak ordering requires > rather than >=.
struct EarliestFirst {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time == b.time){
            return a.seq > b.seq;
        }
        return a.time > b.time;
    }
};

// Wraps std::priority_queue to own the sequence counter, so no caller can
// forget to stamp one and silently reintroduce nondeterminism.
class EventQueue {
public:
    // Events scheduled earlier win ties at equal time.
    void schedule(Tick time, EventPayload payload) {
        heap_.emplace(time, next_seq_++, std::move(payload));
    }

    // top() and pop() are separate in the standard library, so the event has
    // to be copied out before the heap is popped.
    //Caller responsible for ensuring queue isn't empty
    Event pop() {
        assert(!empty());
        Event top = heap_.top();
        heap_.pop();
        return top;
    }

    bool empty() const { return heap_.empty(); }

    std::size_t size() const { return heap_.size(); }

private:
    std::priority_queue<Event, std::vector<Event>, EarliestFirst> heap_;
    std::uint64_t next_seq_ = 0;
};

// Stands in for real unit tests
int main() {
    EventQueue q;

    // Scheduled out of order, with a three-way tie at t=1s. Trailing comments
    // are the sequence number each call consumes.
    q.schedule(500 * kMillisecond, JobArrival{JobId{1}});            // seq 0
    q.schedule(2 * kSecond, JobFinish{JobId{1}, NodeId{0}});         // seq 1
    q.schedule(1 * kSecond, JobArrival{JobId{2}});                   // seq 2
    q.schedule(1 * kSecond, JobArrival{JobId{3}});                   // seq 3
    q.schedule(1 * kSecond, JobArrival{JobId{4}});                   // seq 4
    q.schedule(10 * kSecond, SimEnd{});                              // seq 5

    // Time ordering pulls seq 1 behind the three events at t=1s, which the
    // tie-break then holds in scheduling order.
    const std::uint64_t expected_seq[] = {0, 2, 3, 4, 1, 5};
    constexpr std::size_t kExpectedCount = 6;

    if (q.size() != kExpectedCount) {
        std::printf("FAIL  expected %zu events queued, found %zu\n",
                    kExpectedCount, q.size());
        return 1;
    }

    bool ok = true;
    Tick clock = 0;

    for (std::size_t i = 0; i < kExpectedCount; ++i) {
        const Event e = q.pop();

        std::printf("t=%8.3fs  seq=%llu  %-10s", static_cast<double>(e.time) / 1e6,
                    static_cast<unsigned long long>(e.seq),
                    kEventNames[e.payload.index()]);

        if (e.seq != expected_seq[i]) {
            std::printf("   <-- expected seq=%llu",
                        static_cast<unsigned long long>(expected_seq[i]));
            ok = false;
        }
        if (e.time < clock) {
            std::printf("   <-- CLOCK WENT BACKWARDS");
            ok = false;
        }
        clock = e.time;
        std::printf("\n");
    }

    if (!q.empty()) {
        std::printf("FAIL  queue should be empty, %zu remain\n", q.size());
        ok = false;
    }

    std::printf("\n%s\n", ok ? "PASS  events pop in (time, seq) order"
                             : "FAIL  see markers above");
    return ok ? 0 : 1;
}
