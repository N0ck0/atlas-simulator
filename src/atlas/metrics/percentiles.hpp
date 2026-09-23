#pragma once

#include <vector>

#include "atlas/engine/clock.hpp"

namespace atlas {

struct Percentiles {
    Tick p50 = 0;
    Tick p95 = 0;
    Tick p99 = 0;
};

// Order statistics of `values`, which is REORDERED IN PLACE: pass a copy if
// the original order still matters.
//
// Nearest-rank convention: the pth percentile is the smallest value at or
// above which p percent of the sample lies, at index ceil(p*n/100) - 1. So
// p99 of 100 samples is the 99th, not the largest. Every cross-scheduler
// comparison from S3 on depends on this convention staying fixed.
// Precondition: values is non-empty.
Percentiles percentiles(std::vector<Tick>& values);

}  // namespace atlas
