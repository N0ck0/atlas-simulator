#include "atlas/io/compare.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>
#include <vector>

#include "atlas/engine/simulator.hpp"
#include "atlas/metrics/offered_load.hpp"
#include "atlas/metrics/percentiles.hpp"
#include "atlas/metrics/time_sample.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/profiles.hpp"
#include "atlas/model/workload.hpp"
#include "atlas/sched/factory.hpp"
#include "atlas/sched/queue_factory.hpp"

namespace atlas {

// A sample with nothing in it has no percentile, and percentiles() says so as
// a precondition. Zero is the honest reading here: censored is reported beside
// it, so a row of zeros next to a full censored count is legible rather than
// silently plausible.
static Tick p95_or_zero(std::vector<Tick> times) {
    if (times.empty()) return 0;
    return percentiles(times).p95;
}

Comparison compare_schedulers(const Options& opts, std::uint32_t seed_count) {
    const std::span<const std::string_view> sched = scheduler_names();
    const std::span<const std::string_view> queues = queue_policy_names();
    const std::size_t configs = sched.size() * queues.size();

    Comparison res{};
    res.base_seed = opts.seed;
    res.seeds = seed_count;
    res.offered_rho.resize(seed_count);
    res.rows.assign(configs, std::vector<RunResult>(seed_count));

    // Queue policy varies slowest, so fifo's four rows come first and the
    // baseline of the table is the pre-backfill configuration.
    for (std::size_t q = 0; q < queues.size(); ++q) {
        for (std::size_t sc = 0; sc < sched.size(); ++sc) {
            res.labels.push_back(std::string(queues[q]) + "/" + std::string(sched[sc]));
        }
    }

    for (std::uint32_t k = 0; k < seed_count; ++k) {
        for (std::size_t i = 0; i < configs; ++i) {
            const std::string_view queue_name = queues[i / sched.size()];
            const std::string_view sched_name = sched[i % sched.size()];
            // Regenerated per run rather than hoisted out of the inner loop:
            // Simulator takes ownership of both, and a policy that mutated a
            // shared cluster would leak into the next scheduler's row and
            // quietly become the thing the table measures.
            Cluster cluster{opts.nodes, kDefaultResources};
            WorkloadGenerator gen(opts.seed + k, opts.arrival_rate, opts.estimate_padding);
            std::vector<Job> jobs = gen.generate(opts.jobs);

            Simulator sim(std::move(cluster), std::move(jobs), nullptr, make_scheduler(sched_name),
                          make_queue_policy(queue_name));
            sim.run();

            const TimeSample turnaround = turnaround_times(sim.jobs());
            RunResult& cell = res.rows[i][k];
            cell.p95_turnaround = p95_or_zero(turnaround.times);
            cell.p95_queue_wait = p95_or_zero(queue_wait_times(sim.jobs()).times);
            cell.utilization = sim.mean_utilization();
            cell.censored = turnaround.skipped;
            cell.backfilled = sim.backfilled_count();

            // Offered load reads the jobs and the cluster's capacity, neither
            // of which placement touches, so it is identical down column k and
            // is computed once there.
            if (i == 0) res.offered_rho[k] = offered_load(sim.jobs(), sim.cluster());
        }
    }
    return res;
}

static double seconds(Tick t) { return static_cast<double>(t) / static_cast<double>(kSecond); }

// Population standard deviation, not the sample estimator: these are all the
// seeds that were run, not a draw from a larger set of them.
static double stddev(const std::vector<double>& values, double mean) {
    if (values.size() < 2) return 0.0;
    double sum_sq = 0.0;
    for (double v : values) sum_sq += (v - mean) * (v - mean);
    return std::sqrt(sum_sq / static_cast<double>(values.size()));
}

// Which scheduler had the lowest p95 turnaround on each seed. Ties go to the
// lower index, so the counts sum to the seed count and the winner of a tie is
// the same on every run rather than depending on iteration luck.
static std::vector<std::size_t> win_counts(const Comparison& cmp) {
    std::vector<std::size_t> wins(cmp.rows.size(), 0);
    for (std::uint32_t k = 0; k < cmp.seeds; ++k) {
        std::size_t best = 0;
        for (std::size_t i = 1; i < cmp.rows.size(); ++i) {
            if (cmp.rows[i][k].p95_turnaround < cmp.rows[best][k].p95_turnaround) best = i;
        }
        ++wins[best];
    }
    return wins;
}

static double mean_of(const std::vector<double>& values) {
    double sum = 0.0;
    for (double v : values) sum += v;
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

// The difference against the baseline row, seed by seed, summarised. Pairing
// is the whole point: rows[i][k] and rows[baseline][k] ran the *same* workload,
// so subtracting them cancels that seed's difficulty -- which is the term that
// dominates the unpaired sd column and hides everything else. What survives is
// the effect of the policy alone.
struct PairedDelta {
    double mean = 0.0;  // seconds; negative is faster than the baseline
    double se = 0.0;    // standard error of that mean
};

static PairedDelta paired_delta(const Comparison& cmp, std::size_t row, std::size_t baseline) {
    std::vector<double> deltas;
    deltas.reserve(cmp.seeds);
    for (std::uint32_t k = 0; k < cmp.seeds; ++k) {
        deltas.push_back(seconds(cmp.rows[row][k].p95_turnaround) -
                         seconds(cmp.rows[baseline][k].p95_turnaround));
    }
    PairedDelta out;
    out.mean = mean_of(deltas);
    // The spread of a mean of n draws, not the spread of the draws: dividing by
    // sqrt(n) is what makes this comparable against `mean` rather than against
    // an individual seed.
    out.se = stddev(deltas, out.mean) / std::sqrt(static_cast<double>(cmp.seeds));
    return out;
}

// The table. Ticks become seconds here and nowhere upstream, which is what
// keeps the per-seed comparisons that produced `wins` exact.
void report_comparison(const Comparison& cmp, const Options& opts) {
    const std::vector<std::size_t> wins = win_counts(cmp);

    // Everything is measured against the first name in the factory's table
    // rather than against the best row: a baseline that moved with the result
    // would make two runs of the table incomparable.
    constexpr std::size_t kBaseline = 0;

    std::vector<double> rho_cores, rho_memory;
    for (const LoadFactor& rho : cmp.offered_rho) {
        rho_cores.push_back(rho.cores);
        rho_memory.push_back(rho.memory);
    }

    std::printf("compare: seeds %llu..%llu  nodes %u  jobs %u  rate %g/s\n",
                static_cast<unsigned long long>(cmp.base_seed),
                static_cast<unsigned long long>(cmp.base_seed + cmp.seeds - 1), opts.nodes,
                opts.jobs, opts.arrival_rate);
    std::printf("  offered rho   cores %.4f  memory %.4f  (mean over %u seeds)\n\n",
                mean_of(rho_cores), mean_of(rho_memory), cmp.seeds);

    // Two spreads sit side by side on purpose. `sd` is across seeds and is
    // dominated by how much workloads differ from each other; `± se` is on the
    // paired difference, where that difference has cancelled. A policy is only
    // distinguishable from the baseline when its delta clears a couple of `se`.
    std::printf("  %-23s  %6s  %10s %8s  %10s  %16s  %5s  %7s\n", "queue/scheduler", "util",
                "p95 turn", "sd", "p95 queue", "paired delta", "wins", "backfil");
    std::printf("  %-23s  %6s  %10s %8s  %10s  %16s  %5s  %7s\n", "", "cores", "mean (s)", "(s)",
                "mean (s)", "mean (s) +- se", "", "");

    for (std::size_t i = 0; i < cmp.rows.size(); ++i) {
        std::vector<double> turn, queue, util_cores;
        std::size_t backfilled = 0;
        for (const RunResult& r : cmp.rows[i]) {
            turn.push_back(seconds(r.p95_turnaround));
            queue.push_back(seconds(r.p95_queue_wait));
            util_cores.push_back(r.utilization.cores);
            backfilled += r.backfilled;
        }
        const double turn_mean = mean_of(turn);

        char delta[32];
        if (i == kBaseline) {
            std::snprintf(delta, sizeof(delta), "%16s", "baseline");
        } else {
            const PairedDelta d = paired_delta(cmp, i, kBaseline);
            std::snprintf(delta, sizeof(delta), "%+9.1f +-%5.1f", d.mean, d.se);
        }

        std::printf("  %-23s  %6.4f  %10.1f %8.1f  %10.1f  %16s  %2zu/%-2u  %7zu\n",
                    cmp.labels[i].c_str(), mean_of(util_cores), turn_mean, stddev(turn, turn_mean),
                    mean_of(queue), delta, wins[i], cmp.seeds, backfilled / cmp.seeds);
    }
}

}  // namespace atlas
