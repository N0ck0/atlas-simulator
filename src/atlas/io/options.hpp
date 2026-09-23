#pragma once

#include <cstdint>

namespace atlas {

// Run parameters, all overridable from the command line so that rho can be
// swept without a recompile.
struct Options {
    std::uint64_t seed = 7;
    std::uint32_t nodes = 8;
    std::uint32_t jobs = 2'000;
    double arrival_rate = 0.025;  // jobs per second
    const char* trace_path = nullptr;
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
