#pragma once

namespace atlas {

// Offered load as a fraction of capacity, per dimension. At or above 1.0 the
// cluster cannot keep up: the queue grows without bound and mean completion
// time stops being a property of the scheduler.
struct LoadFactor {
    double cores = 0.0;
    double memory = 0.0;
};

}  // namespace atlas
