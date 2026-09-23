// Atlas: discrete-event simulator for cluster scheduling policies.
//
// Stage 1 lives in one translation unit; CMake and real headers arrive in S2.
// Steps 1.1-1.4: time, events, event queue, cluster, random source, workload,
// and the simulator loop that drives them.
//
// Build:
//   g++ -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2
//       -o atlas src/atlas.cpp && ./atlas

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <queue>
#include <random>
#include <cmath>
#include <variant>
#include <vector>
#include <cassert>
#include <utility>
#include <limits>
#include <optional>
#include <type_traits>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <string>
#include <format>

// Simulated time in microseconds. Integer rather than floating point so that
// arithmetic stays exact and results are identical across optimization levels.
using Tick = std::uint64_t;

constexpr Tick kMicrosecond = 1;
constexpr Tick kMillisecond = 1'000;
constexpr Tick kSecond      = 1'000'000;

// Distinct types, layout-identical to uint32_t, so a JobId cannot be passed
// where a NodeId is expected. Construct with JobId{7}.
enum class JobId : std::uint32_t {};
enum class NodeId : std::uint32_t {};

// Payloads carry IDs, never pointers into mutable state: an event can sit in
// the queue for a long stretch of simulated time before it fires.

struct JobArrival {
    JobId job;
};

struct JobFinish {
    JobId job;
    NodeId node;
};

struct SimEnd {};

// A variant rather than a class hierarchy. The set of event kinds is closed,
// so visitors can be checked for exhaustiveness at compile time, and value
// semantics keep events inline in the queue's vector with no heap allocation.
using EventPayload = std::variant<JobArrival, JobFinish, SimEnd>;
template <typename...>
constexpr bool kAlwaysFalse = false;

// Indexed by EventPayload::index(). Keep in sync with the variant above.
constexpr const char* kEventNames[] = {"JobArrival", "JobFinish", "SimEnd"};

struct Event {
    Tick time;
    std::uint64_t seq;  // insertion order; breaks ties at equal time
    EventPayload payload;
};

// Run parameters, all overridable from the command line so that rho can be
// swept without a recompile.
struct Options {
    std::uint64_t seed = 7;
    std::uint32_t nodes = 8;
    std::uint32_t jobs = 2'000;
    double arrival_rate = 0.025;  // jobs per second
    const char* trace_path = nullptr;
};

// Earliest time first, then insertion order. std::priority_queue is a max-heap
// whose comparator means "a is less important than b", so both tests are
// inverted. Strict weak ordering requires > rather than >=.
struct EarliestFirst {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time == b.time){
            return a.seq > b.seq;
        }
        return a.time > b.time;
    }
};

// Wraps std::priority_queue to own the sequence counter, so no caller can
// forget to stamp one and silently reintroduce nondeterminism.
class EventQueue {
public:
    // Events scheduled earlier win ties at equal time.
    void schedule(Tick time, EventPayload payload) {
        heap_.emplace(time, next_seq_++, std::move(payload));
    }

    // top() and pop() are separate in the standard library, so the event has
    // to be copied out before the heap is popped.
    Event pop() {
        assert(!empty());
        Event top = heap_.top();
        heap_.pop();
        return top;
    }

    bool empty() const { return heap_.empty(); }

    std::size_t size() const { return heap_.size(); }

private:
    std::priority_queue<Event, std::vector<Event>, EarliestFirst> heap_;
    std::uint64_t next_seq_ = 0;
};

// A bundle of resource dimensions, used for both node capacity and job
// requests. Grouping them keeps adding a dimension (storage in S5) to a single
// edit here rather than a change at every signature and call site.
struct Resources {
    std::uint32_t cores = 0;
    std::uint64_t memory_mb = 0;

    // C++20 defaulted comparison: generates member-wise == and !=.
    bool operator==(const Resources&) const = default;
};

// A Tick that has not been set. Distinguishes "not started" from t=0.
constexpr Tick kNever = std::numeric_limits<Tick>::max();

// A machine. `capacity` is fixed at construction and `free` is the only
// mutable resource field, so used == capacity - free is always derivable and
// cannot drift out of sync.
struct Node {
    NodeId id{};
    Resources capacity{};
    Resources free{};
    std::uint32_t running_jobs = 0;

    // True when every dimension of `request` fits in what is currently free.
    bool can_fit(const Resources& request) const {
        return request.cores <= free.cores && request.memory_mb <= free.memory_mb;
    }
};

enum class JobState : std::uint8_t { Queued, Running, Done };

// A unit of work. `request` is the receipt for its resources: allocate and
// release both read this one field, so they cannot disagree about how much was
// taken.
struct Job {
    JobId id{};
    Resources request{};
    Tick duration = 0;  // service time once it starts running
    Tick submit_time = 0;
    Tick start_time = kNever;
    Tick finish_time = kNever;
    NodeId node{};  // meaningful only once state != Queued
    JobState state = JobState::Queued;

    bool operator==(const Job&) const = default;
};

struct TimeSample {
    std::size_t skipped;
    std::vector<Tick> times;
};

// Records the turnaround times of each job
// Includes amount of jobs that didn't finish in skipped, unfinished jobs don't appear returned vector
// Silently dropping skipped jobs would bias times downward without explanation
TimeSample turnaround_times(const std::vector<Job>& jobs){
    std::vector<Tick> res;
    std::size_t skipped_jobs = 0;
    for (const Job& job : jobs){
        if (job.state != JobState::Done){
            skipped_jobs++;
            continue;
        }
        res.push_back(job.finish_time - job.submit_time);
    }
    return TimeSample{skipped_jobs, std::move(res)};
}

// Records the queue wait times of each job
// Includes amount of jobs that didn't start in return struct
TimeSample queue_wait_times(const std::vector<Job>& jobs){
    std::vector<Tick> res;
    std::size_t skipped_jobs = 0;
    for (const Job& job : jobs){
        if (job.start_time == kNever){
            skipped_jobs++;
            continue;
        }
        res.push_back(job.start_time - job.submit_time);
    }
    return TimeSample{skipped_jobs, std::move(res)};
}

// The machines and the resource accounting over them. Owns no jobs; the
// simulator holds those and passes a Resources request in by value.
class Cluster {
public:
    Cluster(std::uint32_t node_count, Resources per_node) {
        nodes_.reserve(node_count);
        for (std::uint32_t i = 0; i < node_count; ++i) {
            nodes_.push_back(Node{NodeId{i}, per_node, per_node, 0});
        }
    }

    // Lowest-index node that can fit `request`, or nullopt if none can.
    // First-fit is the Stage 1 placeholder; S3 replaces it with a pluggable
    // Scheduler interface.
    std::optional<NodeId> first_fit(const Resources& request) const {
        for (const Node& node : nodes_){
            if (node.can_fit(request)) {
                return node.id;
            }
        }
        return std::nullopt;
    }

    // Deducts `request` from the node's free pool and counts a running job.
    // Precondition: node(id).can_fit(request).
    void allocate(NodeId id, const Resources& request) {
        Node& cur_node = mutable_node(id);
        assert(cur_node.can_fit(request));


        cur_node.free.cores -= request.cores;
        cur_node.free.memory_mb -= request.memory_mb;
        cur_node.running_jobs++;
    }

    // Returns `request` to the node's free pool. Must be passed the same
    // Resources that were allocated, or free drifts away from capacity.
    // Precondition: the node has at least one running job, and the release
    // cannot push any dimension of free past capacity.
    void release(NodeId id, const Resources& request) {
        Node& cur_node = mutable_node(id);
        assert(cur_node.running_jobs > 0);
        assert(request.cores <= cur_node.capacity.cores - cur_node.free.cores);
        assert(request.memory_mb <= cur_node.capacity.memory_mb - cur_node.free.memory_mb);

        cur_node.free.cores += request.cores;
        cur_node.free.memory_mb += request.memory_mb;
        cur_node.running_jobs--;

    }

    const Node& node(NodeId id) const {
        return nodes_[static_cast<std::uint32_t>(id)];
    }

    std::size_t size() const { return nodes_.size(); }

    Resources total_capacity() const {
        Resources sum;
        for (const Node& n : nodes_) {
            sum.cores += n.capacity.cores;
            sum.memory_mb += n.capacity.memory_mb;
        }
        return sum;
    }

    // Summed free resources. Used below as a leak detector, and by the
    // utilization metrics in S5.
    Resources total_free() const {
        Resources free_sum;
        for (const Node& n : nodes_) {
            free_sum.cores += n.free.cores;
            free_sum.memory_mb += n.free.memory_mb;
        }
        return free_sum;
    }

    // Returns resources currently in use (utilized). 
    // Derived from total_capacity() and total_free()
    Resources total_utilized() const { 
        Resources utilized{};
        const Resources free = total_free();
        const Resources total = total_capacity();
        utilized.cores = total.cores - free.cores;
        utilized.memory_mb = total.memory_mb - free.memory_mb;
        return utilized;
    }

private:
    Node& mutable_node(NodeId id) {
        return nodes_[static_cast<std::uint32_t>(id)];
    }

    std::vector<Node> nodes_;
};

// Stand in for real unit tests, which arrive with GoogleTest in S2.

// Deterministic source of workload randomness.
//
// Uses the engine's raw output with hand-rolled transforms instead of
// <random>'s distribution classes. The standard pins mt19937_64's output
// sequence exactly, but leaves each distribution's mapping implementation
// defined, so std::exponential_distribution yields different values on
// libstdc++ and libc++ from the same seed. Hand-rolling keeps golden traces
// portable across toolchains.
class RandomSource {
public:
    explicit RandomSource(std::uint64_t seed) : rng_(seed) {}

    // Uniform in (0, 1]. Zero is excluded so callers may take a logarithm.
    double next_uniform() {
        static constexpr double kPow53 = static_cast<double>(1ULL << 53);
        std::uint64_t random = rng_();
        random >>= 11;
        random++;
        return static_cast<double>(random)/kPow53;
    }

    // Exponentially distributed gap with the given mean, in Ticks. Returns 0
    // when a draw rounds below one microsecond; simultaneity is handled by the
    // (time, seq) tie-break.
    // Precondition: mean_ticks > 0.
    Tick next_exponential(double mean_ticks) {
        assert(mean_ticks > 0.0);

        // Inverse transform sampling: for U uniform on (0, 1], -ln(U) * mean
        // is exponentially distributed with that mean. next_uniform() excludes
        // zero, so the logarithm is always defined and the result is never
        // negative.
        const double gap_ticks = -std::log(next_uniform()) * mean_ticks;
        return static_cast<Tick>(gap_ticks);
    }

    // Time until the next event in a Poisson process of `rate_per_second`.
    // Precondition: rate_per_second > 0.
    Tick next_interarrival(double rate_per_second) {
        assert(rate_per_second > 0.0);
        return next_exponential((1/rate_per_second)* static_cast<double>(kSecond));
    }

    // Uniform integer in [0, n). Carries negligible modulo bias at the scales
    // this is used for (choosing among a handful of job profiles).
    // Precondition: n > 0.
    std::uint64_t next_below(std::uint64_t n) {
        assert(n > 0);
        return rng_() % n;
    }

private:
    std::mt19937_64 rng_;
};

static bool check_random_source() {
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

        std::printf("    mean gap %.1f us (expected %.1f), CV %.3f (expected 1.0)\n",
                    mean, expected_mean, cv);
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

    std::printf("%s  random source: determinism, range, distribution\n",
                ok ? "PASS" : "FAIL");
    return ok;
}

// A class of job. The workload draws from a mix of these so that placement is
// a real decision rather than a formality.
struct JobProfile {
    const char* name;
    Resources request;
    Tick mean_duration;
};

// Picked uniformly. Weighting these is how a heavy tail gets added in S7.
constexpr JobProfile kJobProfiles[] = {
    {"small", {1u, 2'048u}, 30 * kSecond},
    {"medium", {4u, 8'192u}, 120 * kSecond},
    {"large", {16u, 32'768u}, 600 * kSecond},
};
constexpr Resources kDefaultResources{16u, 65'536u};

// Offered load as a fraction of capacity, per dimension. At or above 1.0 the
// cluster cannot keep up: the queue grows without bound and mean completion
// time stops being a property of the scheduler.
struct LoadFactor {
    double cores = 0.0;
    double memory = 0.0;
};

// Builds the whole workload before the clock starts, so the job sequence is a
// function of the seed alone and not of any scheduling decision.
class WorkloadGenerator {
public:
    WorkloadGenerator(std::uint64_t seed, double arrival_rate_per_second)
        : rng_(seed), arrival_rate_(arrival_rate_per_second) {
        assert(arrival_rate_per_second > 0.0);
    }

    // `count` jobs with dense ids 0..count-1 and non-decreasing submit_time.
    std::vector<Job> generate(std::uint32_t count) {
        std::vector<Job> jobs;
        jobs.reserve(count);
        Tick cur_tick{0};

        for (std::uint32_t num = 0; num < count; num++){
            const JobProfile& profile = kJobProfiles[rng_.next_below(std::size(kJobProfiles))];
            const Tick duration = rng_.next_exponential(static_cast<double>(profile.mean_duration));
            cur_tick += rng_.next_interarrival(arrival_rate_);
            Job j{
                .id = JobId{num},
                .request = profile.request,
                .duration = duration,
                .submit_time = cur_tick
            };
            jobs.push_back(j);
        }

        return jobs;
    }

private:
    RandomSource rng_;
    double arrival_rate_;
};

// Resource-time demanded by `jobs` over the span they arrive in, divided by
// what `cluster` can supply in that span.
// Precondition: `jobs` is non-empty and ordered by submit_time.
LoadFactor offered_load(const std::vector<Job>& jobs, const Cluster& cluster) {
    assert(jobs.size() > 0);
    Tick arrival_start = jobs[0].submit_time;
    Tick arrival_end = jobs[jobs.size() - 1].submit_time;
    Tick arrival_span = arrival_end - arrival_start;
    Resources cap = cluster.total_capacity();
    std::uint64_t free_cores = cap.cores;
    std::uint64_t free_memory_mb = cap.memory_mb;
    std::uint64_t core_seconds = free_cores * arrival_span;
    std::uint64_t memory_seconds = free_memory_mb * arrival_span;

    std::uint64_t job_cores_seconds = 0;
    std::uint64_t job_memory_seconds = 0;
    for (const Job& job : jobs){
        job_cores_seconds += job.duration * job.request.cores;
        job_memory_seconds += job.duration * job.request.memory_mb;
    }
    double core_load = static_cast<double>(job_cores_seconds) / 
                        static_cast<double>(core_seconds);
    double memory_load = static_cast<double>(job_memory_seconds) / 
                        static_cast<double>(memory_seconds);

    return {core_load, memory_load};
}

static bool check_workload() {
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
            if (j.state != JobState::Queued || j.start_time != kNever ||
                j.finish_time != kNever) {
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
        const double span = static_cast<double>(jobs.back().submit_time -
                                                jobs.front().submit_time);
        const double mean_gap = span / static_cast<double>(jobs.size() - 1);
        const double expected_gap = static_cast<double>(kSecond) / kRate;
        expect(std::fabs(mean_gap - expected_gap) / expected_gap < 0.05,
               "mean arrival gap matches the configured rate");
        std::printf("    mean arrival gap %.0f us (expected %.0f)\n", mean_gap,
                    expected_gap);
    }

    // Every profile should show up across 20k uniform draws.
    {
        constexpr std::size_t kProfileCount =
            sizeof(kJobProfiles) / sizeof(kJobProfiles[0]);
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

    std::printf("%s  workload: determinism, structure, rate, load factor\n",
                ok ? "PASS" : "FAIL");
    return ok;
}

// Time-weighted integral of resources in use, Advanced at
// every event, never sampled.
struct UtilizationIntegral {
    private:
    Tick last_checkin_ = 0;
    std::uint64_t core_ticks_ = 0;
    //potential overflow risk? maybe convert to gb or regular seconds?
    std::uint64_t memory_ticks_ = 0;

    public:
    // Advances the integral to `now`, crediting the interval since the last
    // advance to `in_use`.
    // Precondition: now >= the last time advanced.
    void advance(Tick now, const Resources& in_use);

    // Mean fraction of `capacity` in use over [0, end], per dimension.
    LoadFactor mean(Tick end, const Resources& capacity) const;
};

void UtilizationIntegral::advance(Tick now, const Resources& in_use) {
    assert(now >= last_checkin_);
    if (now == last_checkin_) return;
    Tick interval = now - last_checkin_;
    core_ticks_ += interval * in_use.cores;
    memory_ticks_ += interval * in_use.memory_mb;
    last_checkin_ = now;
}

LoadFactor UtilizationIntegral::mean(Tick end, const Resources& capacity) const {
    assert(end > 0);
    std::uint64_t total_core_ticks_ = end * capacity.cores;
    std::uint64_t total_memory_ticks_ = end * capacity.memory_mb;
    assert(total_core_ticks_ > 0);
    assert(total_memory_ticks_ > 0);
    assert(end != kNever);
    return LoadFactor{static_cast<double>(core_ticks_) / static_cast<double>(total_core_ticks_), 
        static_cast<double>(memory_ticks_) / static_cast<double>(total_memory_ticks_)};
}

struct Percentiles {
    Tick p50 = 0;
    Tick p95 = 0;
    Tick p99 = 0;
};

// Order statistics of `values`, which is REORDERED IN PLACE: pass a copy if
// the original order still matters.
//
// Nearest-rank convention: the pth percentile is the smallest value at or
// above which p percent of the sample lies, at index ceil(p*n/100) - 1. So
// p99 of 100 samples is the 99th, not the largest. Every cross-scheduler
// comparison from S3 on depends on this convention staying fixed.
// Precondition: values is non-empty.
Percentiles percentiles(std::vector<Tick>& values);

// JSONL event trace: one JSON object per line, so a run is diffable and can be
// processed a record at a time rather than parsed whole. Writes nothing when
// Options::trace_path is null.
//
// Output is block-buffered, so the file is only complete once the writer is
// destroyed; scope it accordingly if something else is going to read it.
class TraceWriter {
public:
    explicit TraceWriter(const Options& opt);
    ~TraceWriter();
    TraceWriter(const TraceWriter&) = delete;
    TraceWriter& operator=(const TraceWriter&) = delete;

    void write(const Event& e);

    // A job's static properties, logged once before the clock starts so that
    // analysis can group by job size without replaying the seed.
    void job_spec(const Job& j);

    // Placement is not an event: drain() assigns a job to a node as a side
    // effect of handling an arrival or a finish, so nothing reaches write().
    // Logging it here is what lets the trace separate queue wait from service
    // time. These records carry no seq -- they never entered the queue -- so
    // file order, not the seq field, is what orders a tick's lines.
    void placement(Tick t, JobId job, NodeId node);

private:
    static std::string to_jsonl(const Event& e);
    void write_line(const std::string& line);

    std::FILE* out_ = nullptr;
};

std::string TraceWriter::to_jsonl(const Event& e) {
    std::string line = std::format(R"({{"t":{},"seq":{})", e.time, e.seq);

    line += std::visit([](const auto& p) -> std::string {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, JobArrival>) {
            return std::format(R"(,"ev":"JobArrival","job":{})",
                               static_cast<std::uint32_t>(p.job));
        } else if constexpr (std::is_same_v<T, JobFinish>) {
            return std::format(R"(,"ev":"JobFinish","job":{},"node":{})",
                               static_cast<std::uint32_t>(p.job),
                               static_cast<std::uint32_t>(p.node));
        } else if constexpr (std::is_same_v<T, SimEnd>) {
            return R"(,"ev":"SimEnd")";
        } else {
            static_assert(kAlwaysFalse<T>, "Unhandled event type in trace writer");
        }
    }, e.payload);

    line += "}";
    return line;
}

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

    return Percentiles{values[static_cast<std::size_t>(i50)],
                       values[static_cast<std::size_t>(i95)],
                       values[static_cast<std::size_t>(i99)]};
}

TraceWriter::TraceWriter(const Options& opt) {
    const char* path = opt.trace_path;
    if (path == nullptr) return;
    out_ = std::fopen(path, "w");
    if (out_ == nullptr){
        std::perror(path);
        return;
    }
    write_line(std::format(R"({{"ev":"header","seed":{},"nodes":{},"jobs":{},"arrival_rate":{}}})",
         opt.seed, opt.nodes, opt.jobs, opt.arrival_rate));
}

// Every record goes through here, so the one-object-per-line invariant is
// enforced in a single place rather than at each call site.
void TraceWriter::write_line(const std::string& line) {
    if (out_ == nullptr) return;
    std::fputs(line.c_str(), out_);
    std::fputs("\n", out_);
}

TraceWriter::~TraceWriter() {
    if (out_ != nullptr) std::fclose(out_);
}

void TraceWriter::write(const Event& e) {
    if (out_ == nullptr) return;
    write_line(to_jsonl(e));
}

void TraceWriter::job_spec(const Job& j) {
    if (out_ == nullptr) return;
    write_line(std::format(
        R"({{"ev":"job","job":{},"submit":{},"dur":{},"cores":{},"mem_mb":{}}})",
        static_cast<std::uint32_t>(j.id), j.submit_time, j.duration,
        j.request.cores, j.request.memory_mb));
}

void TraceWriter::placement(Tick t, JobId job, NodeId node) {
    if (out_ == nullptr) return;
    write_line(std::format(R"({{"t":{},"ev":"JobStart","job":{},"node":{}}})", t,
                           static_cast<std::uint32_t>(job),
                           static_cast<std::uint32_t>(node)));
}

// Drives the clock. Owns the jobs and the backlog; Cluster still never sees a
// Job, only a Resources request.
//
// pending_ holds ids rather than pointers because jobs_ is a vector: a Job*
// parked in the backlog would dangle the moment it reallocated.
class Simulator {
public:
    // `jobs` must have dense ids matching its indices, as WorkloadGenerator
    // produces: jobs_[5] is JobId{5}.
    Simulator(Cluster cluster, std::vector<Job> jobs, TraceWriter* trace = nullptr);

    // Seeds the queue and runs it to exhaustion. Call once.
    void run();

    Tick now() const { return now_; }
    const std::vector<Job>& jobs() const { return jobs_; }
    const Cluster& cluster() const { return cluster_; }
    std::size_t pending_count() const { return pending_.size(); }

    // Valid only after run() has returned.
    LoadFactor mean_utilization() const;

private:
    // The one and only placement site. Nothing else calls Cluster::allocate.
    void drain();

    void on_arrival(const JobArrival& arrival);
    void on_finish(const JobFinish& finish);
    void on_sim_end(const SimEnd& end);

    Job& job(JobId id) { return jobs_[static_cast<std::uint32_t>(id)]; }

    Cluster cluster_;
    std::vector<Job> jobs_;
    std::deque<JobId> pending_;
    EventQueue queue_;
    Tick now_ = 0;
    UtilizationIntegral util_;
    Tick sim_end_ = kNever;
    TraceWriter* trace_ = nullptr;
};

Simulator::Simulator(Cluster cluster, std::vector<Job> jobs, TraceWriter* trace)
    : cluster_(std::move(cluster)), jobs_(std::move(jobs)), trace_(trace) {}

LoadFactor Simulator::mean_utilization() const {
    assert(sim_end_ < kNever);
    return util_.mean(sim_end_, cluster_.total_capacity());
}

void Simulator::run() {
    for (const Job& j : jobs_){
        queue_.schedule(j.submit_time, JobArrival{j.id});
        if (trace_ != nullptr) trace_->job_spec(j);
    }

    while (!queue_.empty()){
        bool should_end = false;
        Event e = queue_.pop();
        assert(e.time >= now_);
        now_ = e.time;
        if (trace_ != nullptr){
            trace_->write(e);
        }
        util_.advance(now_, cluster_.total_utilized());

        std::visit([this, &should_end](auto& payload){

            using T = std::decay_t<decltype(payload)>;

            if constexpr(std::is_same_v<T, JobArrival>){
                this->on_arrival(payload);
            }
            else if constexpr (std::is_same_v<T, JobFinish>){
                this->on_finish(payload);
            }
            else if constexpr (std::is_same_v<T, SimEnd>){
                this->on_sim_end(payload);
                should_end = true;
            }
            else{
                static_assert(kAlwaysFalse<T>, "Unhandled Event Type in Visitor");
            }
            }, e.payload);

        if (should_end){
            break;
        }

        if (queue_.empty()){
            queue_.schedule(now_, SimEnd{});
        }
    }


}

void Simulator::drain() {
    while (!pending_.empty()){
        JobId next_job_id = pending_.front();
        Job& next_job = job(next_job_id);
        auto res = cluster_.first_fit(next_job.request);
        if (!res.has_value()) break;

        //Job at front of pending queue can be assigned
        cluster_.allocate(res.value(), next_job.request);
        next_job.state = JobState::Running;
        next_job.start_time = now_;
        next_job.node = res.value();
        if (trace_ != nullptr) trace_->placement(now_, next_job.id, next_job.node);
        pending_.pop_front();
        queue_.schedule(now_ + next_job.duration, JobFinish{next_job.id, next_job.node});
    }
}

void Simulator::on_arrival(const JobArrival& arrival) {
    pending_.push_back(arrival.job);
    drain();
}

void Simulator::on_sim_end(const SimEnd& end){
    util_.advance(now_, cluster_.total_utilized());
    sim_end_ = now_;
    (void) end;
}

void Simulator::on_finish(const JobFinish& finish) {
    Job& this_job = job(finish.job);
    assert(this_job.state == JobState::Running);
    assert(this_job.node == finish.node);
    cluster_.release(this_job.node, this_job.request);
    this_job.finish_time = now_;
    this_job.state = JobState::Done;
    drain();
}


static bool check_simulator() {
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
                        static_cast<double>(queued_time) /
                            static_cast<double>(started) / 1e6,
                        started);
        }
    }

    std::printf("%s  simulator loop: completion, conservation, determinism\n",
                ok ? "PASS" : "FAIL");
    return ok;
}

static bool check_event_queue() {
    EventQueue q;

    // Scheduled out of order, with a three-way tie at t=1s. Trailing comments
    // are the sequence number each call consumes.
    q.schedule(500 * kMillisecond, JobArrival{JobId{1}});     // seq 0
    q.schedule(2 * kSecond, JobFinish{JobId{1}, NodeId{0}});   // seq 1
    q.schedule(1 * kSecond, JobArrival{JobId{2}});             // seq 2
    q.schedule(1 * kSecond, JobArrival{JobId{3}});             // seq 3
    q.schedule(1 * kSecond, JobArrival{JobId{4}});             // seq 4
    q.schedule(10 * kSecond, SimEnd{});                        // seq 5

    // Time ordering pulls seq 1 behind the three events at t=1s, which the
    // tie-break then holds in scheduling order.
    const std::uint64_t expected_seq[] = {0, 2, 3, 4, 1, 5};
    constexpr std::size_t kExpectedCount = 6;

    if (q.size() != kExpectedCount) {
        std::printf("FAIL  expected %zu events queued, found %zu\n",
                    kExpectedCount, q.size());
        return false;
    }

    bool ok = true;
    Tick clock = 0;

    for (std::size_t i = 0; i < kExpectedCount; ++i) {
        const Event e = q.pop();

        std::printf("  t=%8.3fs  seq=%llu  %-10s",
                    static_cast<double>(e.time) / 1e6,
                    static_cast<unsigned long long>(e.seq),
                    kEventNames[e.payload.index()]);

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

static bool check_cluster() {
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
    expect(c.total_capacity() == Resources{32u, 65'536u},
           "total capacity is 4x per-node");
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
        expect(c.node(*placed).running_jobs == 1u,
               "allocate counts a running job");
    }

    expect(!c.first_fit(Resources{1u, 1u}).has_value(),
           "a full cluster places nothing");
    expect(c.total_free() == Resources{}, "a full cluster has no free resources");

    // The leak check. Releasing everything must restore the starting state
    // exactly; any asymmetry between allocate and release surfaces here.
    for (std::uint32_t i = 0; i < 4u; ++i) {
        c.release(NodeId{i}, kWholeNode);
    }
    expect(c.total_free() == c.total_capacity(),
           "release restores the cluster exactly");
    expect(c.node(NodeId{0}).running_jobs == 0u, "release clears the job count");

    std::printf("%s  cluster model: fit, placement, allocate/release\n",
                ok ? "PASS" : "FAIL");
    return ok;
}

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

static void print_usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s [options]\n"
                 "  with no options, runs the built-in self-checks instead.\n\n"
                 "  --seed N      random seed (default 7)\n"
                 "  --nodes N     nodes in the cluster (default 8)\n"
                 "  --jobs N      jobs to generate (default 2000)\n"
                 "  --rate F      arrivals per second (default 0.025)\n"
                 "  --trace PATH  write a JSONL event trace to PATH\n"
                 "  --help        this message\n",
                 program);
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
    const char* problem = nullptr;
    if (opts.nodes == 0) problem = "--nodes must be at least 1";
    else if (opts.jobs == 0) problem = "--jobs must be at least 1";
    else if (!(opts.arrival_rate > 0.0)) problem = "--rate must be positive";
    if (problem != nullptr) {
        std::fprintf(stderr, "%s: %s\n", program, problem);
        return ParsedArgs{ParsedArgs::Status::Error, {}};
    }

    return ParsedArgs{ParsedArgs::Status::Run, opts};
}

static double seconds(Tick t) {
    return static_cast<double>(t) / static_cast<double>(kSecond);
}

// Takes the sample by value because percentiles() reorders what it is given:
// the copy makes that explicit at the call site rather than surprising a
// caller who still wanted the original order.
static void report_sample(const char* label, TimeSample sample) {
    if (sample.times.empty()) {
        std::printf("  %-12s  no samples        (%zu censored)\n", label,
                    sample.skipped);
        return;
    }
    const Percentiles p = percentiles(sample.times);
    std::printf("  %-12s  p50 %8.1fs  p95 %8.1fs  p99 %8.1fs  (%zu censored)\n",
                label, seconds(p.p50), seconds(p.p95), seconds(p.p99),
                sample.skipped);
}

// Human-readable summary of a finished run. Ticks become seconds only here;
// the trace keeps raw integers so its hash stays stable across builds.
// Precondition: sim.run() has returned.
void report(const Simulator& sim, const Options& opts) {
    const LoadFactor rho = offered_load(sim.jobs(), sim.cluster());
    const LoadFactor util = sim.mean_utilization();

    std::printf("run: seed %llu  nodes %u  jobs %u  rate %g/s\n",
                static_cast<unsigned long long>(opts.seed), opts.nodes,
                opts.jobs, opts.arrival_rate);
    std::printf("  span          %12.1fs\n", seconds(sim.now()));
    std::printf("  offered rho   cores %.4f  memory %.4f\n", rho.cores, rho.memory);
    std::printf("  utilization   cores %.4f  memory %.4f\n", util.cores, util.memory);
    report_sample("turnaround", turnaround_times(sim.jobs()));
    report_sample("queue wait", queue_wait_times(sim.jobs()));
}

static bool check_metrics() {
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
        std::printf("    unqueued run: util cores %.6f (expected %.6f)\n",
                    got.cores, expected_cores);
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
        return static_cast<double>(total) /
               static_cast<double>(sample.times.size()) / 1e6;
    };

    // Capacity relieves queueing. A metric that moves the wrong way here is
    // measuring something other than what it claims to.
    {
        const double small = mean_queue_wait(8u, kRate);
        const double large = mean_queue_wait(16u, kRate);
        expect(large < small, "more nodes lowers mean queue time");
        std::printf("    queue wait: 8 nodes %.1fs -> 16 nodes %.1fs\n", small,
                    large);
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

    std::printf("%s  metrics: utilization integral, queueing response\n",
                ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    const ParsedArgs parsed = parse_args(argc, argv);
    if (parsed.status == ParsedArgs::Status::HelpRequested) return 0;
    if (parsed.status == ParsedArgs::Status::Error) return 2;

    // Arguments mean "run the simulation"; bare `atlas` runs the self-checks,
    // which is what `make run` relies on.
    if (argc > 1){
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
    return (queue_ok && cluster_ok && random_ok && workload_ok && simulator_ok &&
            metrics_ok)
               ? 0
               : 1;
}
