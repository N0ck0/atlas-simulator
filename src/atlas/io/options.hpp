#pragma once

#include <cstdint>

#include "atlas/model/profiles.hpp"

namespace atlas {

// Run parameters, all overridable from the command line so that rho can be
// swept without a recompile.
struct Options {
    std::uint64_t seed = 7;
    std::uint32_t nodes = 8;
    std::uint32_t jobs = 2'000;
    double arrival_rate = 0.025;  // jobs per second
    const char* trace_path = nullptr;

    // Validated against scheduler_names() during parsing, so by the time an
    // Options reaches the simulator this is guaranteed constructible.
    const char* scheduler = "first_fit";

    // Which job runs next, as opposed to which node it lands on. Defaults to
    // fifo so that a bare `atlas` is the pre-backfill baseline.
    const char* queue = "fifo";

    // Widest over-claim in a walltime estimate: estimates are uniform on
    // [1, 1 + padding] times the true duration. Only backfill reads them, but
    // they are drawn for every run so the workload does not depend on the
    // policy.
    double estimate_padding = kDefaultEstimatePadding;

    // --compare runs every scheduler over `seeds` consecutive seeds starting
    // at `seed` and prints a table instead of one run's report. `scheduler` is
    // then unused: comparing policies means running all of them.
    bool compare = false;
    std::uint32_t seeds = 30;
};

// A command line has three outcomes, not two: run with these options, print
// usage and stop successfully (--help), or reject. std::optional collapses the
// middle case into the error case, which is why --help used to exit 2.
struct ParsedArgs {
    enum class Status : std::uint8_t { Run, HelpRequested, Error };

    Status status = Status::Error;
    Options options{};
};

// Prints usage on both --help and rejection; `options` is meaningful only when
// status is Run.
ParsedArgs parse_args(int argc, char** argv);

}  // namespace atlas
