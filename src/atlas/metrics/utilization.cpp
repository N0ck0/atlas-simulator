#include "atlas/metrics/utilization.hpp"

#include <cassert>

namespace atlas {

void UtilizationIntegral::advance(Tick now, const Resources& in_use) {
    assert(now >= last_checkin_);
    if (now == last_checkin_) return;
    Tick interval = now - last_checkin_;
    core_ticks_ += interval * in_use.cores;
    memory_ticks_ += interval * in_use.memory_mb;
    last_checkin_ = now;
}

LoadFactor UtilizationIntegral::mean(Tick end, const Resources& capacity) const {
    assert(end > 0);
    std::uint64_t total_core_ticks_ = end * capacity.cores;
    std::uint64_t total_memory_ticks_ = end * capacity.memory_mb;
    assert(total_core_ticks_ > 0);
    assert(total_memory_ticks_ > 0);
    assert(end != kNever);
    return LoadFactor{
        static_cast<double>(core_ticks_) / static_cast<double>(total_core_ticks_),
        static_cast<double>(memory_ticks_) / static_cast<double>(total_memory_ticks_)};
}

}  // namespace atlas
