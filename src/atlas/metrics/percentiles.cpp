#include "atlas/metrics/percentiles.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace atlas {

// Index of the pth percentile under the nearest-rank convention.
// Precondition: n > 0 and 0 < p <= 100.
static std::ptrdiff_t percentile_rank(std::size_t n, std::size_t p) {
    assert(n > 0 && p > 0 && p <= 100);
    const std::size_t index = (n * p + 99) / 100 - 1;  // ceil(n*p/100) - 1
    return static_cast<std::ptrdiff_t>(index);
}

Percentiles percentiles(std::vector<Tick>& values) {
    assert(!values.empty());

    const std::size_t n = values.size();
    const std::ptrdiff_t i50 = percentile_rank(n, 50);
    const std::ptrdiff_t i95 = percentile_rank(n, 95);
    const std::ptrdiff_t i99 = percentile_rank(n, 99);

    // Descending order, each call restricted to the prefix the previous one
    // already isolated: nth_element leaves everything below its pivot on the
    // left, so the smaller quantiles cannot have moved out of that prefix.
    // Three full-range calls would also be correct, just more work.
    const auto begin = values.begin();
    std::nth_element(begin, begin + i99, values.end());
    std::nth_element(begin, begin + i95, begin + i99);
    std::nth_element(begin, begin + i50, begin + i95);

    return Percentiles{values[static_cast<std::size_t>(i50)], values[static_cast<std::size_t>(i95)],
                       values[static_cast<std::size_t>(i99)]};
}

}  // namespace atlas
