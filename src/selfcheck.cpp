#include "selfcheck.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "atlas/engine/clock.hpp"
#include "atlas/engine/event.hpp"
#include "atlas/engine/event_queue.hpp"
#include "atlas/engine/ids.hpp"
#include "atlas/engine/simulator.hpp"
#include "atlas/io/options.hpp"
#include "atlas/metrics/load_factor.hpp"
#include "atlas/metrics/offered_load.hpp"
#include "atlas/metrics/percentiles.hpp"
#include "atlas/metrics/time_sample.hpp"
#include "atlas/metrics/utilization.hpp"
#include "atlas/model/cluster.hpp"
#include "atlas/model/job.hpp"
#include "atlas/model/profiles.hpp"
#include "atlas/model/random.hpp"
#include "atlas/model/resources.hpp"
#include "atlas/model/workload.hpp"

namespace atlas {

bool check_event_queue() {
    EventQueue q;

    // Scheduled out of order, with a three-way tie at t=1s. Trailing comments
    // are the sequence number each call consumes.
    q.schedule(500 * kMillisecond, JobArrival{JobId{1}});     // seq 0
    q.schedule(2 * kSecond, JobFinish{JobId{1}, NodeId{0}});  // seq 1
    q.schedule(1 * kSecond, JobArrival{JobId{2}});            // seq 2
    q.schedule(1 * kSecond, JobArrival{JobId{3}});            // seq 3
    q.schedule(1 * kSecond, JobArrival{JobId{4}});            // seq 4
    q.schedule(10 * kSecond, SimEnd{});                       // seq 5

    // Time ordering pulls seq 1 behind the three events at t=1s, which the
    // tie-break then holds in scheduling order.
    const std::uint64_t expected_seq[] = {0, 2, 3, 4, 1, 5};
    constexpr std::size_t kExpectedCount = 6;

    if (q.size() != kExpectedCount) {
        std::printf("FAIL  expected %zu events queued, found %zu\n", kExpectedCount, q.size());
        return false;
    }

    bool ok = true;
    Tick clock = 0;

    for (std::size_t i = 0; i < kExpectedCount; ++i) {
        const Event e = q.pop();

        std::printf("  t=%8.3fs  seq=%llu  %-10s", static_cast<double>(e.time) / 1e6,
                    static_cast<unsigned long long>(e.seq), kEventNames[e.payload.index()]);

        if (e.seq != expected_seq[i]) {
            std::printf("   <-- expected seq=%llu",
                        static_cast<unsigned long long>(expected_seq[i]));
            ok = false;
        }
        if (e.time < clock) {
            std::printf("   <-- CLOCK WENT BACKWARDS");
            ok = false;
        }
        clock = e.time;
        std::printf("\n");
    }

    if (!q.empty()) {
        std::printf("  FAIL  queue should be empty, %zu remain\n", q.size());
        ok = false;
    }

    std::printf("%s  event queue: (time, seq) ordering\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool check_cluster() {
    bool ok = true;
    auto expect = [&ok](bool cond, const char* what) {
        if (!cond) {
            std::printf("  FAIL  %s\n", what);
            ok = false;
        }
    };

    constexpr Resources kPerNode{8u, 16'384u};
    Cluster c(4u, kPerNode);

    expect(c.size() == 4u, "cluster has 4 nodes");
    expect(c.total_capacity() == Resources{32u, 65'536u}, "total capacity is 4x per-node");
    expect(c.total_free() == c.total_capacity(), "a fresh cluster is fully free");

    // Dimensions are independent: a request can fail on memory alone.
    const Node& n0 = c.node(NodeId{0});
    expect(n0.can_fit(Resources{8u, 16'384u}), "an exact fit is accepted");
    expect(!n0.can_fit(Resources{9u, 1'024u}), "too many cores is rejected");
    expect(!n0.can_fit(Resources{1u, 20'000u}), "too much memory is rejected");

    // first_fit takes the lowest index that fits, and moves along as nodes
    // fill up.
    constexpr Resources kWholeNode{8u, 16'384u};
    for (std::uint32_t i = 0; i < 4u; ++i) {
        const std::optional<NodeId> placed = c.first_fit(kWholeNode);
        expect(placed.has_value(), "a free node was found");
        if (!placed) {
            std::printf("FAIL  cluster model\n");
            return false;
        }
        expect(*placed == NodeId{i}, "first_fit returns the lowest free index");
        c.allocate(*placed, kWholeNode);
        expect(c.node(*placed).running_jobs == 1u, "allocate counts a running job");
    }

    expect(!c.first_fit(Resources{1u, 1u}).has_value(), "a full cluster places nothing");
    expect(c.total_free() == Resources{}, "a full cluster has no free resources");

    // The leak check. Releasing everything must restore the starting state
    // exactly; any asymmetry between allocate and release surfaces here.
    for (std::uint32_t i = 0; i < 4u; ++i) {
        c.release(NodeId{i}, kWholeNode);
    }
    expect(c.total_free() == c.total_capacity(), "release restores the cluster exactly");
    expect(c.node(NodeId{0}).running_jobs == 0u, "release clears the job count");

    std::printf("%s  cluster model: fit, placement, allocate/release\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool check_random_source() {
    bool ok = true;
    auto expect = [&ok](bool cond, const char* what) {
        if (!cond) {
            std::printf("  FAIL  %s\n", what);
            ok = false;
        }
    };

    constexpr std::uint64_t kSeed = 42;
    constexpr std::size_t kSamples = 200'000;
    constexpr double kRate = 0.025;  // jobs per second -> mean gap 100'000 us

    // Determinism: the same seed must replay the same sequence exactly. This
    // is the property every reproducible experiment rests on.
    {
        RandomSource a(kSeed);
        RandomSource b(kSeed);
        bool identical = true;
        for (std::size_t i = 0; i < 1'000; ++i) {
            if (a.next_uniform() != b.next_uniform()) identical = false;
        }
        expect(identical, "same seed replays an identical sequence");
    }

    // A different seed must actually diverge, or the seed is being ignored.
    {
        RandomSource a(kSeed);
        RandomSource b(kSeed + 1);
        bool any_difference = false;
        for (std::size_t i = 0; i < 1'000; ++i) {
            if (a.next_uniform() != b.next_uniform()) any_difference = true;
        }
        expect(any_difference, "a different seed produces a different sequence");
    }

    // Range: (0, 1]. Zero would break the logarithm in next_interarrival.
    {
        RandomSource r(kSeed);
        bool in_range = true;
        for (std::size_t i = 0; i < kSamples; ++i) {
            const double u = r.next_uniform();
            if (!(u > 0.0 && u <= 1.0)) in_range = false;
        }
        expect(in_range, "every uniform draw lies in (0, 1]");
    }

    // Statistical checks. Randomness is tested by distribution, not by exact
    // value, so each comes with a tolerance derived from the sample count.
    {
        RandomSource r(kSeed);
        double sum = 0.0;
        double sum_sq = 0.0;
        bool monotonic_ok = true;
        for (std::size_t i = 0; i < kSamples; ++i) {
            const double gap = static_cast<double>(r.next_interarrival(kRate));
            if (gap < 0.0) monotonic_ok = false;
            sum += gap;
            sum_sq += gap * gap;
        }
        const double n = static_cast<double>(kSamples);
        const double mean = sum / n;
        const double variance = sum_sq / n - mean * mean;
        const double stddev = std::sqrt(variance);
        const double expected_mean = 1e6 / kRate;  // microseconds

        expect(monotonic_ok, "no negative inter-arrival gaps");

        // Relative standard error of the mean is 1/sqrt(N) ~ 0.22% here, so a
        // 2% band is about 9 sigma: tight enough to catch a wrong rate, loose
        // enough never to fail by chance.
        const double mean_error = std::fabs(mean - expected_mean) / expected_mean;
        expect(mean_error < 0.02, "mean inter-arrival gap matches 1/rate");

        // The exponential distribution has stddev == mean, so its coefficient
        // of variation is 1. A uniform distribution with the right mean would
        // give ~0.577, so this check verifies the SHAPE, not just the average.
        const double cv = stddev / mean;
        expect(cv > 0.95 && cv < 1.05, "gap distribution is exponential (CV ~ 1)");

        std::printf("    mean gap %.1f us (expected %.1f), CV %.3f (expected 1.0)\n", mean,
                    expected_mean, cv);
    }

    // next_below: in range, degenerate case, and roughly even coverage.
    {
        RandomSource r(kSeed);
        constexpr std::uint64_t kBuckets = 5;
        constexpr std::size_t kDraws = 100'000;
        std::vector<std::size_t> counts(kBuckets, 0);
        bool in_range = true;
        for (std::size_t i = 0; i < kDraws; ++i) {
            const std::uint64_t v = r.next_below(kBuckets);
            if (v >= kBuckets) {
                in_range = false;
            } else {
                ++counts[v];
            }
        }
        expect(in_range, "next_below stays under n");

        // Expected 20'000 per bucket, sigma ~126, so a 10% band is ~16 sigma.
        bool even = true;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            const double share = static_cast<double>(counts[i]) / static_cast<double>(kDraws);
            if (share < 0.18 || share > 0.22) even = false;
        }
        expect(even, "next_below covers each value roughly evenly");

        RandomSource one(kSeed);
        expect(one.next_below(1) == 0, "next_below(1) is always 0");
    }

    std::printf("%s  random source: determinism, range, distribution\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool check_workload() {
    bool ok = true;
    auto expect = [&ok](bool cond, const char* what) {
        if (!cond) {
            std::printf("  FAIL  %s\n", what);
            ok = false;
        }
    };

    constexpr std::uint64_t kSeed = 7;
    constexpr std::uint32_t kCount = 20'000;
    constexpr double kRate = 0.025;  // jobs per second

    // Determinism: the workload must be a pure function of the seed.
    {
        WorkloadGenerator g1(kSeed, kRate);
        WorkloadGenerator g2(kSeed, kRate);
        expect(g1.generate(kCount) == g2.generate(kCount),
               "same seed produces an identical workload");

        WorkloadGenerator g3(kSeed + 1, kRate);
        WorkloadGenerator g4(kSeed, kRate);
        expect(!(g3.generate(kCount) == g4.generate(kCount)),
               "a different seed produces a different workload");
    }

    WorkloadGenerator gen(kSeed, kRate);
    const std::vector<Job> jobs = gen.generate(kCount);

    expect(jobs.size() == kCount, "generate returns the requested count");
    if (jobs.size() != kCount) {
        std::printf("FAIL  workload generator\n");
        return false;
    }

    // Structural invariants every generated job must satisfy.
    {
        bool ids_dense = true;
        bool times_sorted = true;
        bool fresh = true;
        bool has_duration = true;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            const Job& j = jobs[i];
            if (static_cast<std::size_t>(static_cast<std::uint32_t>(j.id)) != i) {
                ids_dense = false;
            }
            if (i > 0 && j.submit_time < jobs[i - 1].submit_time) {
                times_sorted = false;
            }
            if (j.state != JobState::Queued || j.start_time != kNever || j.finish_time != kNever) {
                fresh = false;
            }
            if (j.duration == 0 || j.request.cores == 0) {
                has_duration = false;
            }
        }
        expect(ids_dense, "ids are dense and in order");
        expect(times_sorted, "submit times are non-decreasing");
        expect(fresh, "every job starts Queued with unset timestamps");
        expect(has_duration, "every job has a non-zero duration and request");
    }

    // Arrival rate calibration, read back off the submit times.
    {
        const double span = static_cast<double>(jobs.back().submit_time - jobs.front().submit_time);
        const double mean_gap = span / static_cast<double>(jobs.size() - 1);
        const double expected_gap = static_cast<double>(kSecond) / kRate;
        expect(std::fabs(mean_gap - expected_gap) / expected_gap < 0.05,
               "mean arrival gap matches the configured rate");
        std::printf("    mean arrival gap %.0f us (expected %.0f)\n", mean_gap, expected_gap);
    }

    // Every profile should show up across 20k uniform draws.
    {
        constexpr std::size_t kProfileCount = sizeof(kJobProfiles) / sizeof(kJobProfiles[0]);
        std::vector<std::size_t> seen(kProfileCount, 0);
        for (const Job& j : jobs) {
            for (std::size_t p = 0; p < kProfileCount; ++p) {
                if (j.request == kJobProfiles[p].request) ++seen[p];
            }
        }
        bool all_present = true;
        for (std::size_t c : seen) {
            if (c == 0) all_present = false;
        }
        expect(all_present, "every job profile appears in the workload");
    }

    // Load factor. Checked by its scaling properties rather than by
    // recomputing the formula, which would just restate the implementation.
    {
        Cluster small(8u, Resources{16u, 65'536u});
        const LoadFactor lf = offered_load(jobs, small);
        std::printf("    rho: cores %.3f  memory %.3f\n", lf.cores, lf.memory);
        expect(lf.cores > 0.0 && lf.memory > 0.0, "load factor is positive");
        expect(lf.cores < 1000.0, "Rho within plausible range");

        // Doubling capacity must halve rho.
        Cluster big(16u, Resources{16u, 65'536u});
        const LoadFactor lf_big = offered_load(jobs, big);
        expect(std::fabs(lf_big.cores - lf.cores / 2.0) < 0.01 * lf.cores,
               "rho halves when cluster capacity doubles");

        // Doubling the arrival rate must roughly double rho: the same jobs
        // arrive over half the span.
        WorkloadGenerator fast(kSeed, kRate * 2.0);
        const LoadFactor lf_fast = offered_load(fast.generate(kCount), small);
        const double ratio = lf_fast.cores / lf.cores;
        expect(ratio > 1.8 && ratio < 2.2, "rho scales with arrival rate");
        std::printf("    rho doubles with rate: ratio %.3f\n", ratio);
    }

    std::printf("%s  workload: determinism, structure, rate, load factor\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool check_simulator() {
    bool ok = true;
    auto expect = [&ok](bool cond, const char* what) {
        if (!cond) {
            std::printf("  FAIL  %s\n", what);
            ok = false;
        }
    };

    constexpr std::uint64_t kSeed = 7;
    constexpr std::uint32_t kCount = 2'000;
    constexpr double kRate = 0.025;
    constexpr Resources kPerNode{16u, 65'536u};

    WorkloadGenerator gen(kSeed, kRate);
    const std::vector<Job> workload = gen.generate(kCount);

    Simulator sim(Cluster(8u, kPerNode), workload);
    sim.run();

    const std::vector<Job>& done = sim.jobs();

    expect(done.size() == kCount, "the simulator keeps every job");
    if (done.size() != kCount) {
        std::printf("FAIL  simulator loop\n");
        return false;
    }

    // Every job must reach Done with a consistent, causal set of timestamps.
    {
        bool all_done = true;
        bool durations_honoured = true;
        bool causal = true;
        bool request_preserved = true;
        for (std::size_t i = 0; i < done.size(); ++i) {
            const Job& j = done[i];
            const Job& original = workload[i];
            if (j.state != JobState::Done) all_done = false;
            if (j.start_time == kNever || j.finish_time == kNever) {
                causal = false;
                continue;
            }
            if (j.finish_time - j.start_time != j.duration) {
                durations_honoured = false;
            }
            if (j.start_time < j.submit_time) causal = false;
            if (j.request != original.request || j.duration != original.duration ||
                j.submit_time != original.submit_time) {
                request_preserved = false;
            }
        }
        expect(all_done, "every job ends in the Done state");
        expect(causal, "no job starts before it was submitted");
        expect(durations_honoured, "each job runs for exactly its duration");
        expect(request_preserved, "the simulator does not rewrite the workload");
    }

    expect(sim.pending_count() == 0, "nothing is left queued at the end");

    // The leak check, same idea as the cluster one: if allocate and release
    // ever disagree, or a job is placed twice, free no longer returns to
    // capacity.
    expect(sim.cluster().total_free() == sim.cluster().total_capacity(),
           "the cluster is fully free once the run ends");

    // The clock must end no earlier than the last completion.
    {
        Tick last_finish = 0;
        for (const Job& j : done) {
            if (j.finish_time != kNever && j.finish_time > last_finish) {
                last_finish = j.finish_time;
            }
        }
        expect(sim.now() >= last_finish, "the clock ends at or after the last finish");
    }

    // Determinism, the headline property: the same seed and cluster must
    // replay an identical history.
    {
        Simulator again(Cluster(8u, kPerNode), workload);
        again.run();
        expect(again.jobs() == done, "the same seed replays an identical history");
    }

    // A cluster large enough to hold the whole workload at once should queue
    // nothing: every job starts the moment it arrives. This separates "the
    // loop works" from "the loop happens to be backlogged".
    {
        Simulator roomy(Cluster(kCount, kPerNode), workload);
        roomy.run();
        bool no_queueing = true;
        for (const Job& j : roomy.jobs()) {
            if (j.start_time != j.submit_time) no_queueing = false;
        }
        expect(no_queueing, "an oversized cluster starts every job on arrival");
    }

    // A cluster too small for any job must not place one, and must not hang.
    {
        Simulator tiny(Cluster(1u, Resources{1u, 1'024u}), workload);
        tiny.run();
        bool none_started = true;
        for (const Job& j : tiny.jobs()) {
            if (j.start_time != kNever) none_started = false;
        }
        expect(none_started, "a cluster that fits nothing places nothing");
        expect(tiny.pending_count() == kCount, "unplaceable jobs stay pending");
    }

    // Queueing must show up under load, otherwise the backlog path is dead
    // code and every check above passes vacuously.
    {
        Tick queued_time = 0;
        std::size_t started = 0;
        for (const Job& j : done) {
            if (j.start_time == kNever) continue;
            queued_time += j.start_time - j.submit_time;
            ++started;
        }
        expect(queued_time > 0, "some job waits at rho 0.67");
        if (started > 0) {
            std::printf("    mean queue time %.3fs over %zu started jobs\n",
                        static_cast<double>(queued_time) / static_cast<double>(started) / 1e6,
                        started);
        }
    }

    std::printf("%s  simulator loop: completion, conservation, determinism\n",
                ok ? "PASS" : "FAIL");
    return ok;
}

bool check_metrics() {
    bool ok = true;
    auto expect = [&ok](bool cond, const char* what) {
        if (!cond) {
            std::printf("  FAIL  %s\n", what);
            ok = false;
        }
    };

    constexpr std::uint64_t kSeed = 7;
    constexpr std::uint32_t kCount = 2'000;
    constexpr double kRate = 0.025;
    constexpr Resources kPerNode{16u, 65'536u};

    WorkloadGenerator gen(kSeed, kRate);
    const std::vector<Job> workload = gen.generate(kCount);

    // A cluster with a node per job queues nothing, so each job holds its
    // resources for exactly its duration and the integral collapses to a sum
    // over the workload. That sum is computed here from the job list alone,
    // without replaying the event loop, which is what makes this a check on
    // the accumulation rather than a restatement of it.
    {
        Simulator roomy(Cluster(kCount, kPerNode), workload);
        roomy.run();

        std::uint64_t core_ticks = 0;
        std::uint64_t memory_ticks = 0;
        for (const Job& j : workload) {
            core_ticks += j.duration * j.request.cores;
            memory_ticks += j.duration * j.request.memory_mb;
        }

        const Resources cap = roomy.cluster().total_capacity();
        const double span = static_cast<double>(roomy.now());
        const double expected_cores =
            static_cast<double>(core_ticks) / (span * static_cast<double>(cap.cores));
        const double expected_memory =
            static_cast<double>(memory_ticks) / (span * static_cast<double>(cap.memory_mb));

        const LoadFactor got = roomy.mean_utilization();

        // Both sides are exact integer sums turned into doubles by a single
        // division, so the only slack needed is floating-point rounding.
        expect(std::fabs(got.cores - expected_cores) < 1e-9 * expected_cores,
               "core integral equals the workload's core-time");
        expect(std::fabs(got.memory - expected_memory) < 1e-9 * expected_memory,
               "memory integral equals the workload's memory-time");
        std::printf("    unqueued run: util cores %.6f (expected %.6f)\n", got.cores,
                    expected_cores);
    }

    // Mean rather than a percentile: at these cluster sizes the median queue
    // wait bottoms out at zero and stops discriminating, so it cannot detect
    // an improvement that only affects the tail.
    auto mean_queue_wait = [](std::uint32_t nodes, double rate) {
        WorkloadGenerator g(kSeed, rate);
        Simulator sim(Cluster(nodes, kPerNode), g.generate(kCount));
        sim.run();
        const TimeSample sample = queue_wait_times(sim.jobs());
        assert(sample.skipped == 0);  // censored waits would not be comparable
        Tick total = 0;
        for (Tick t : sample.times) total += t;
        return static_cast<double>(total) / static_cast<double>(sample.times.size()) / 1e6;
    };

    // Capacity relieves queueing. A metric that moves the wrong way here is
    // measuring something other than what it claims to.
    {
        const double small = mean_queue_wait(8u, kRate);
        const double large = mean_queue_wait(16u, kRate);
        expect(large < small, "more nodes lowers mean queue time");
        std::printf("    queue wait: 8 nodes %.1fs -> 16 nodes %.1fs\n", small, large);
    }

    // Queueing must grow faster than load does. Three equally spaced rates:
    // if wait time were merely proportional to rho the two increments would
    // match, so convexity is what separates a queueing system from a
    // stopwatch. Compared as increments rather than ratios because the first
    // wait can be near zero.
    {
        const double w1 = mean_queue_wait(8u, 0.025);
        const double w2 = mean_queue_wait(8u, 0.030);
        const double w3 = mean_queue_wait(8u, 0.035);
        expect(w1 < w2 && w2 < w3, "queue time grows with arrival rate");
        expect((w3 - w2) > (w2 - w1), "queue time grows superlinearly as rho -> 1");
        std::printf("    queue wait vs rate: %.1fs -> %.1fs -> %.1fs\n", w1, w2, w3);
    }

    // The third acceptance check, identical output across -O0 and -O2, needs
    // two binaries and so cannot live here: see `make check-determinism`.

    std::printf("%s  metrics: utilization integral, queueing response\n", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace atlas
