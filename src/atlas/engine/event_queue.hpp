#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/event.hpp"

namespace atlas {

// Earliest time first, then insertion order. std::priority_queue is a max-heap
// whose comparator means "a is less important than b", so both tests are
// inverted. Strict weak ordering requires > rather than >=.
struct EarliestFirst {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time == b.time) {
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

}  // namespace atlas
