// Atlas: discrete-event simulator for cluster scheduling policies.
//
// Stage 1 lives in one translation unit; CMake and real headers arrive in S2.
// Steps 1.1-1.3b: time, events, event queue, cluster, random source, workload.
//
// Build:
//   g++ -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2
//       -o atlas src/atlas.cpp && ./atlas

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <random>
#include <cmath>
#include <variant>
#include <vector>
#include <cassert>
#include <utility>
#include <limits>
#include <optional>

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

// Indexed by EventPayload::index(). Keep in sync with the variant above.
constexpr const char* kEventNames[] = {"JobArrival", "JobFinish", "SimEnd"};

struct Event {
    Tick time;
    std::uint64_t seq;  // insertion order; breaks ties at equal time
    EventPayload payload;
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

int main() {
    const bool queue_ok = check_event_queue();
    const bool cluster_ok = check_cluster();
    const bool random_ok = check_random_source();
    const bool workload_ok = check_workload();
    return (queue_ok && cluster_ok && random_ok && workload_ok) ? 0 : 1;
}
