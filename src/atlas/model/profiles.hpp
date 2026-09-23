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
constexpr JobProfile kJobProfiles[] = {
    {"small", {1u, 2'048u}, 30 * kSecond},
    {"medium", {4u, 8'192u}, 120 * kSecond},
    {"large", {16u, 32'768u}, 600 * kSecond},
};
constexpr Resources kDefaultResources{16u, 65'536u};

}  // namespace atlas
