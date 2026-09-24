#include "atlas/io/options.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <type_traits>

#include "atlas/sched/factory.hpp"
#include "atlas/sched/queue_factory.hpp"

namespace atlas {

static void print_usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s [options]\n\n"
                 "  --seed N        random seed (default 7)\n"
                 "  --nodes N       nodes in the cluster (default 8)\n"
                 "  --jobs N        jobs to generate (default 2000)\n"
                 "  --rate F        arrivals per second (default 0.025)\n"
                 "  --scheduler S   placement policy (default first_fit)\n"
                 "  --queue Q       queue policy (default fifo)\n"
                 "  --padding F     walltime over-claim, uniform [1,1+F]x (default 2)\n"
                 "  --trace PATH    write a JSONL event trace to PATH\n"
                 "  --compare       table over every queue/scheduler pair\n"
                 "  --seeds N       seeds per configuration under --compare (default 30)\n"
                 "  --help          this message\n",
                 program);

    // Generated from the factory's table rather than spelled out above, so a
    // new scheduler appears here without anyone remembering to add it.
    std::fprintf(stderr, "\n  schedulers:");
    for (std::string_view name : scheduler_names()) {
        std::fprintf(stderr, " %.*s", static_cast<int>(name.size()), name.data());
    }
    std::fprintf(stderr, "\n  queues:    ");
    for (std::string_view name : queue_policy_names()) {
        std::fprintf(stderr, " %.*s", static_cast<int>(name.size()), name.data());
    }
    std::fprintf(stderr, "\n");
}

// Parses the whole of `text` into `out`. from_chars rather than atoi/strtoul:
// it reports trailing garbage ("12abc") and overflow instead of silently
// truncating, and it is locale-independent, which matters for --rate.
template <typename T>
static bool parse_value(const char* text, T& out) {
    const char* const last = text + std::strlen(text);
    const auto [ptr, ec] = std::from_chars(text, last, out);
    return ec == std::errc{} && ptr == last;
}

ParsedArgs parse_args(int argc, char** argv) {
    Options opts;
    const char* const program = (argc > 0) ? argv[0] : "atlas";

    for (int i = 1; i < argc; ++i) {
        const char* const flag = argv[i];

        if (std::strcmp(flag, "--help") == 0 || std::strcmp(flag, "-h") == 0) {
            print_usage(program);
            return ParsedArgs{ParsedArgs::Status::HelpRequested, {}};
        }

        // Checked before the bounds check below, which assumes a value follows.
        if (std::strcmp(flag, "--compare") == 0) {
            opts.compare = true;
            continue;
        }

        // Every remaining flag takes a value, so the bounds check lives here
        // once. Without it a trailing "--seed" reads argv[argc], which is the
        // null terminator at best and past the end at worst.
        if (i + 1 >= argc) {
            std::fprintf(stderr, "%s: missing value for %s\n", program, flag);
            print_usage(program);
            return ParsedArgs{ParsedArgs::Status::Error, {}};
        }
        const char* const value = argv[++i];

        bool parsed = true;
        if (std::strcmp(flag, "--seed") == 0) {
            parsed = parse_value(value, opts.seed);
        } else if (std::strcmp(flag, "--nodes") == 0) {
            parsed = parse_value(value, opts.nodes);
        } else if (std::strcmp(flag, "--jobs") == 0) {
            parsed = parse_value(value, opts.jobs);
        } else if (std::strcmp(flag, "--rate") == 0) {
            parsed = parse_value(value, opts.arrival_rate);
        } else if (std::strcmp(flag, "--scheduler") == 0) {
            opts.scheduler = value;
        } else if (std::strcmp(flag, "--queue") == 0) {
            opts.queue = value;
        } else if (std::strcmp(flag, "--padding") == 0) {
            parsed = parse_value(value, opts.estimate_padding);
        } else if (std::strcmp(flag, "--seeds") == 0) {
            parsed = parse_value(value, opts.seeds);
        } else if (std::strcmp(flag, "--trace") == 0) {
            opts.trace_path = value;
        } else {
            std::fprintf(stderr, "%s: unknown option %s\n", program, flag);
            print_usage(program);
            return ParsedArgs{ParsedArgs::Status::Error, {}};
        }

        if (!parsed) {
            std::fprintf(stderr, "%s: bad value for %s: %s\n", program, flag, value);
            return ParsedArgs{ParsedArgs::Status::Error, {}};
        }
    }

    // Range is checked separately from syntax: "0" parses perfectly well and
    // then divides by zero in the utilization denominator, and a zero rate
    // trips an assert inside WorkloadGenerator rather than printing usage.
    // An unknown scheduler is rejected here rather than at construction, so the
    // user gets usage and the list of valid names instead of a null pointer
    // surfacing somewhere inside the simulator.
    if (make_scheduler(opts.scheduler) == nullptr) {
        std::fprintf(stderr, "%s: unknown scheduler %s\n", program, opts.scheduler);
        print_usage(program);
        return ParsedArgs{ParsedArgs::Status::Error, {}};
    }
    if (make_queue_policy(opts.queue) == nullptr) {
        std::fprintf(stderr, "%s: unknown queue policy %s\n", program, opts.queue);
        print_usage(program);
        return ParsedArgs{ParsedArgs::Status::Error, {}};
    }

    const char* problem = nullptr;
    if (opts.nodes == 0)
        problem = "--nodes must be at least 1";
    else if (opts.jobs == 0)
        problem = "--jobs must be at least 1";
    else if (!(opts.arrival_rate > 0.0))
        problem = "--rate must be positive";
    else if (!(opts.estimate_padding >= 0.0))
        problem = "--padding must be zero or positive";
    else if (opts.seeds == 0)
        problem = "--seeds must be at least 1";
    else if (opts.compare && opts.trace_path != nullptr)
        problem = "--compare and --trace are exclusive: a sweep would overwrite one path per run";
    if (problem != nullptr) {
        std::fprintf(stderr, "%s: %s\n", program, problem);
        return ParsedArgs{ParsedArgs::Status::Error, {}};
    }

    return ParsedArgs{ParsedArgs::Status::Run, opts};
}

}  // namespace atlas
