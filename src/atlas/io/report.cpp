#include "atlas/io/report.hpp"

#include <cstdio>

#include "atlas/engine/clock.hpp"
#include "atlas/metrics/offered_load.hpp"
#include "atlas/metrics/percentiles.hpp"
#include "atlas/metrics/time_sample.hpp"

namespace atlas {

static double seconds(Tick t) { return static_cast<double>(t) / static_cast<double>(kSecond); }

// Takes the sample by value because percentiles() reorders what it is given:
// the copy makes that explicit at the call site rather than surprising a
// caller who still wanted the original order.
static void report_sample(const char* label, TimeSample sample) {
    if (sample.times.empty()) {
        std::printf("  %-12s  no samples        (%zu censored)\n", label, sample.skipped);
        return;
    }
    const Percentiles p = percentiles(sample.times);
    std::printf("  %-12s  p50 %8.1fs  p95 %8.1fs  p99 %8.1fs  (%zu censored)\n", label,
                seconds(p.p50), seconds(p.p95), seconds(p.p99), sample.skipped);
}

// Human-readable summary of a finished run. Ticks become seconds only here;
// the trace keeps raw integers so its hash stays stable across builds.
// Precondition: sim.run() has returned.
void report(const Simulator& sim, const Options& opts) {
    const LoadFactor rho = offered_load(sim.jobs(), sim.cluster());
    const LoadFactor util = sim.mean_utilization();

    std::printf("run: seed %llu  nodes %u  jobs %u  rate %g/s  scheduler %s\n",
                static_cast<unsigned long long>(opts.seed), opts.nodes, opts.jobs,
                opts.arrival_rate, opts.scheduler);
    std::printf("  span          %12.1fs\n", seconds(sim.now()));
    std::printf("  offered rho   cores %.4f  memory %.4f\n", rho.cores, rho.memory);
    std::printf("  utilization   cores %.4f  memory %.4f\n", util.cores, util.memory);
    report_sample("turnaround", turnaround_times(sim.jobs()));
    report_sample("queue wait", queue_wait_times(sim.jobs()));
}

}  // namespace atlas
