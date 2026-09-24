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

// Picked uniformly; weighting them is how a heavy tail would be added.
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

// Walltime estimates land uniformly in [1x, 3x] the true duration by default.
// The spread, not the mean, is what makes backfill hard: a uniform bias would
// be invertible, so a scheduler could divide it out and recover the future.
constexpr double kDefaultEstimatePadding = 2.0;

}  // namespace atlas
