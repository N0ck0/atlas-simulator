#pragma once

#include "atlas/engine/clock.hpp"
#include "atlas/model/resources.hpp"

namespace atlas {

// A class of job. The workload draws from a mix of these so that placement is
// a real decision rather than a formality.
struct JobProfile {
    const char* name;
    Resources request;
    Tick mean_duration;
};

// Picked uniformly. Weighting these is how a heavy tail gets added in S7.
//
// The first three are core-dominant in the same proportion -- each asks for
// twice the share of a node's cores as of its memory. A table of only those is
// effectively one-dimensional: free memory is then a fixed function of free
// cores, so every multi-resource policy collapses to the same ranking and a
// scheduler comparison has nothing to distinguish. `cache` exists to break that
// -- it is memory-dominant at three times the share, the shape of an in-memory
// store, and it is what makes the second dimension carry information.
constexpr JobProfile kJobProfiles[] = {
    {"small", {1u, 2'048u}, 30 * kSecond},
    {"medium", {4u, 8'192u}, 120 * kSecond},
    {"large", {16u, 32'768u}, 600 * kSecond},
    {"cache", {4u, 49'152u}, 600 * kSecond},
};
constexpr Resources kDefaultResources{16u, 65'536u};

}  // namespace atlas
