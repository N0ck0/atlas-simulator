#pragma once

#include <vector>

#include "atlas/metrics/load_factor.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"

namespace atlas {

// Resource-time demanded by `jobs` over the span they arrive in, divided by
// what `cluster` can supply in that span.
// Precondition: `jobs` is non-empty and ordered by submit_time.
LoadFactor offered_load(const std::vector<Job>& jobs, const Cluster& cluster);

}  // namespace atlas
