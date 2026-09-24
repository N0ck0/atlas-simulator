#pragma once

#include <cstdint>

#include "atlas/model/resources.hpp"

namespace atlas {

// The larger of a node's two utilization fractions -- its most constrained
// dimension -- kept as an exact rational rather than a double.
//
// Cores and megabytes are not comparable until both are expressed as a share of
// the node they sit on, which is what makes the fractions the unit of
// comparison. Keeping them as (numerator, denominator) rather than dividing is
// invariant 4: floating point is on the determinism watch-list, and a ranking
// that flipped between -O0 and -O3 because two scores rounded differently would
// be exactly the kind of bug the golden trace exists to catch. Cross
// multiplication answers the same question exactly, and the values here are far
// too small to overflow 64 bits.
struct DominantShare {
    std::uint64_t used = 0;
    std::uint64_t capacity = 1;

    // True when this share is strictly smaller than `other`, by
    // used/capacity < other.used/other.capacity.
    bool operator<(const DominantShare& other) const {
        return used * other.capacity < other.used * capacity;
    }
};

// `used` out of `capacity`, on whichever dimension is the tighter of the two.
inline DominantShare dominant_share(const Resources& used, const Resources& capacity) {
    const DominantShare cores{used.cores, capacity.cores};
    const DominantShare memory{used.memory_mb, capacity.memory_mb};
    return (cores < memory) ? memory : cores;
}

}  // namespace atlas
