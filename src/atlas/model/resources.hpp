#pragma once

#include <cstdint>

namespace atlas {

// A bundle of resource dimensions, used for both node capacity and job
// requests. Grouping them keeps adding a dimension (storage in S5) to a single
// edit here rather than a change at every signature and call site.
struct Resources {
    std::uint32_t cores = 0;
    std::uint64_t memory_mb = 0;

    // C++20 defaulted comparison: generates member-wise == and !=.
    bool operator==(const Resources&) const = default;
};

}  // namespace atlas
