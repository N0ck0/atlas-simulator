#include <utility>
#include <vector>

#include "atlas/engine/simulator.hpp"
#include "atlas/io/options.hpp"
#include "atlas/io/report.hpp"
#include "atlas/io/trace.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/profiles.hpp"
#include "atlas/model/workload.hpp"
#include "selfcheck.hpp"

using namespace atlas;

int main(int argc, char** argv) {
    const ParsedArgs parsed = parse_args(argc, argv);
    if (parsed.status == ParsedArgs::Status::HelpRequested) return 0;
    if (parsed.status == ParsedArgs::Status::Error) return 2;

    // Arguments mean "run the simulation"; bare `atlas` runs the self-checks,
    // which is what `make run` relies on.
    if (argc > 1) {
        const Options opt = parsed.options;
        Cluster main_cluster = Cluster{opt.nodes, kDefaultResources};
        WorkloadGenerator work_gen = WorkloadGenerator(opt.seed, opt.arrival_rate);
        std::vector<Job> jobs = work_gen.generate(opt.jobs);
        TraceWriter writer{opt};
        Simulator sim = Simulator(std::move(main_cluster), std::move(jobs), &writer);
        sim.run();

        report(sim, opt);
        return 0;
    }

    const bool queue_ok = check_event_queue();
    const bool cluster_ok = check_cluster();
    const bool random_ok = check_random_source();
    const bool workload_ok = check_workload();
    const bool simulator_ok = check_simulator();
    const bool metrics_ok = check_metrics();
    return (queue_ok && cluster_ok && random_ok && workload_ok && simulator_ok && metrics_ok) ? 0
                                                                                              : 1;
}
