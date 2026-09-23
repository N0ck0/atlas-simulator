#pragma once

#include <cstdint>

#include "atlas/engine/ids.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {

// A machine. `capacity` is fixed at construction and `free` is the only
// mutable resource field, so used == capacity - free is always derivable and
// cannot drift out of sync.
struct Node {
    NodeId id{};
    Resources capacity{};
    Resources free{};
    std::uint32_t running_jobs = 0;

    // True when every dimension of `request` fits in what is currently free.
    bool can_fit(const Resources& request) const {
        return request.cores <= free.cores && request.memory_mb <= free.memory_mb;
    }
};

}  // namespace atlas
