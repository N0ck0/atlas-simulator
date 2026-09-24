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
#include "atlas/sched/factory.hpp"

using namespace atlas;

int main(int argc, char** argv) {
    const ParsedArgs parsed = parse_args(argc, argv);
    if (parsed.status == ParsedArgs::Status::HelpRequested) return 0;
    if (parsed.status == ParsedArgs::Status::Error) return 2;

    // Every option has a default, so a bare `atlas` runs one simulation at the
    // default operating point. Correctness checks used to live behind this same
    // entry point; they are GoogleTest cases now, run by ctest.
    const Options opt = parsed.options;
    Cluster main_cluster = Cluster{opt.nodes, kDefaultResources};
    WorkloadGenerator work_gen = WorkloadGenerator(opt.seed, opt.arrival_rate);
    std::vector<Job> jobs = work_gen.generate(opt.jobs);
    TraceWriter writer{opt};

    // parse_args has already rejected an unknown name, so this cannot be null.
    Simulator sim =
        Simulator(std::move(main_cluster), std::move(jobs), &writer, make_scheduler(opt.scheduler));
    sim.run();

    report(sim, opt);
    return 0;
}
