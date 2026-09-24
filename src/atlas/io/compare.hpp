#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/io/options.hpp"
#include "atlas/metrics/load_factor.hpp"

namespace atlas {

// One (scheduler, seed) outcome. Deliberately integer: the mean and standard
// deviation the table prints are floating point, and that arithmetic belongs
// in the report layer. Keeping Tick here also makes "which scheduler won this
// seed" an exact integer comparison rather than a float one.
struct RunResult {
    Tick p95_turnaround = 0;
    Tick p95_queue_wait = 0;
    LoadFactor utilization{};
    std::size_t censored = 0;
    std::size_t backfilled = 0;  // zero under fifo by construction
};

// The whole sweep, rectangular on purpose: rows[i][k] is configuration i on
// seed base_seed + k. A per-seed win count is a comparison down column k,
// which only means anything if every configuration saw that same seed.
//
// A configuration is a (queue policy, placement policy) pair. Which job runs
// next and where it lands are independent choices, and measuring placement
// alone separated the four policies by under half a percent, so the table takes
// their product -- `labels[i]` names the pair.
struct Comparison {
    std::vector<std::string> labels;
    std::uint64_t base_seed = 0;
    std::uint32_t seeds = 0;
    // Indexed by seed, not by scheduler: placement cannot change offered load,
    // so it is identical down a column and belongs above the table rather than
    // in it. It does vary across seeds, which is why this is not one number.
    std::vector<LoadFactor> offered_rho;
    std::vector<std::vector<RunResult>> rows;
};

// Runs every (queue policy, scheduler) pair over seeds base_seed ..
// base_seed + seed_count - 1, holding every other option fixed so that the
// placement policy is the only thing that varies.
//
// Precondition: seed_count >= 1. Tracing is deliberately absent: a sweep would
// write seed_count * schedulers traces over one path, so parse_args rejects
// --compare together with --trace rather than silently picking a winner.
Comparison compare_schedulers(const Options& opts, std::uint32_t seed_count);

// Renders the table: per-scheduler means across seeds, the spread, and how
// many seeds each policy won. The only place the sweep does float arithmetic.
void report_comparison(const Comparison& cmp, const Options& opts);

}  // namespace atlas
