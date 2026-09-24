# Atlas

[![CI](https://github.com/N0ck0/atlas-simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/N0ck0/atlas-simulator/actions/workflows/ci.yml)

A discrete-event simulator for testing datacenter scheduling policies.

> **Status: in development.** Stages 1 to 3 are done. The simulator runs end to end,
> compares eight scheduling configurations across a seed sweep, reports metrics, and
> emits a deterministic trace. There are 103 tests running against four build
> configurations in CI. Unchecked boxes on the roadmap are genuinely not implemented.

## What this is

Atlas models a cluster of machines and a stream of jobs arriving at it, then measures
what a scheduling policy does to them.

Today it gives you:

- **A cluster** of identical nodes, each with cores and memory.
- **A workload** generated from a seed: Poisson arrivals, four job profiles ranging
  from a single core to a whole node, exponential service times.
- **Two scheduling decisions you can swap independently.** One picks which queued job
  runs next (`fifo` or EASY `backfill`). The other picks which node it lands on
  (`first_fit`, `round_robin`, `least_loaded`, `resource_aware`).
- **Metrics** for each run: job turnaround and queue wait at p50/p95/p99, time-weighted
  resource utilization, and the offered load to compare them against.
- **A comparison mode** that runs every policy combination over the same set of seeds
  and reports the difference with enough statistics to tell a real effect from noise.
- **A JSONL trace** of everything that happened, for analysis in pandas or anywhere
  else.

A simulated hour of cluster activity takes milliseconds, because the clock jumps
straight from one event to the next and skips the empty time in between. A run of 2,000
jobs takes about 16ms, which makes it cheap to sweep dozens of seeds and configurations
and treat the output as an experiment.

The same seed always produces byte-identical output, on any compiler, at any
optimization level. A test enforces that, and it is what lets two runs be compared at
all.

## Quick start

Requires a C++20 compiler, CMake 3.25+, and Ninja.

```bash
cmake --preset debug          # also: release, asan
cmake --build --preset debug
./build/debug/atlas           # one simulation with default settings
```

There is a `Makefile` for the commands you type most. Every recipe forwards straight to
`cmake` or `ctest` and holds no build settings of its own, so the two are
interchangeable:

```bash
make            # build the debug preset
make test       # the test suite: 103 GoogleTest cases
make asan       # build with ASan + UBSan
make help       # every target
```

`make sim` runs one simulation and prints a report. Any parameter can be overridden, so
you can move the operating point without recompiling:

```bash
make sim NODES=16 RATE=0.035
```

## A single run

```
run: seed 7  nodes 8  jobs 2000  rate 0.025/s  scheduler first_fit  queue fifo
  span               81161.9s
  offered rho   cores 0.6209  memory 0.6144
  utilization   cores 0.6025  memory 0.5962
  backfilled               0 of 2000 jobs
  turnaround    p50   1258.8s  p95   2745.3s  p99   3642.9s  (0 censored)
  queue wait    p50    980.6s  p95   2228.2s  p99   2487.8s  (0 censored)
```

`offered rho` is how much work the workload asks of the cluster, as a fraction of
capacity. `utilization` is how much the cluster actually delivered, weighted by time.
The two track closely while the cluster keeps up and separate once it cannot. Results
are reported as percentiles because the distributions have long tails that a mean would
hide.

Two flags choose the policy, one for each half of a scheduling decision:

```bash
./build/debug/atlas --queue backfill --scheduler resource_aware
```

`--queue` decides which job runs next. `--scheduler` decides which node it goes to.

### About backfill

Under `fifo`, the job at the front of the queue holds up everything behind it until it
fits. A job needing a whole node can block a one-core job for a long time.

`backfill` implements EASY backfill, the standard fix. When the head of the queue does
not fit, the scheduler works out the earliest moment it could start, then lets a job
further back run ahead of it if that job will finish before the head needs the machine.
The head keeps its slot, and the gap gets used.

Working out "will finish before" needs a guess at how long a job runs. Atlas uses a
walltime estimate, the number a user would have claimed at submission, which real users
over-claim by varying amounts. `--padding F` sets the spread: estimates are drawn
uniformly between 1x and (1+F)x the true service time. The true duration is deliberately
hidden from the scheduler, since planning against it would mean reading a future no real
scheduler can see.

## Comparing policies

`--compare` runs all eight combinations over the same seeds and prints a table:

```
compare: seeds 7..36  nodes 8  jobs 2000  rate 0.025/s
  offered rho   cores 0.6155  memory 0.6041  (mean over 30 seeds)

  queue/scheduler            util    p95 turn       sd   p95 queue      paired delta   wins  backfil
                            cores    mean (s)      (s)    mean (s)    mean (s) +- se
  fifo/first_fit           0.5952      2688.7   1217.6      2084.8          baseline   0/30        0
  fifo/round_robin         0.5953      2682.8   1189.3      2079.5       -6.0 +-  7.9   1/30        0
  fifo/least_loaded        0.5952      2695.9   1194.5      2093.1       +7.1 +-  7.9   0/30        0
  fifo/resource_aware      0.5953      2675.9   1187.1      2072.5      -12.8 +-  7.3   0/30        0
  backfill/first_fit       0.5957      2592.4   1119.3      1753.6      -96.3 +- 23.6  10/30      697
  backfill/round_robin     0.5957      2603.6   1119.3      1755.5      -85.1 +- 23.3   5/30      702
  backfill/least_loaded    0.5956      2607.0   1125.0      1759.4      -81.7 +- 21.9   6/30      708
  backfill/resource_aware  0.5957      2593.5   1118.5      1746.5      -95.2 +- 23.9   8/30      695
```

Every configuration runs the identical workload on each seed, which is what makes the
`paired delta` column meaningful.

**Why there are two spread columns.** Each seed generates a different random workload,
and some workloads are simply harder than others. `sd` measures that: at around 1,200s
it is mostly telling you how much one seed differs from the next, which has nothing to
do with scheduling. `+- se` measures something narrower. Since both policies ran the
same workload on a given seed, subtracting one from the other cancels out that seed's
difficulty and leaves the effect of the policy itself. The same data supports a spread
of 1,200s and an uncertainty of 8s at the same time.

### What the table says

The four `fifo` rows are a null result. The best placement policy is 12.8s ahead of the
baseline with an uncertainty of 7.3s, on a p95 of roughly 2,700s. That is under half a
percent and well inside the noise. Which node a job lands on does not measurably matter
here.

The queue policy does matter. Backfill is 96.3s ahead with an uncertainty of 23.6s, four
times its own error bar. Most of that improvement sits in the middle of the distribution
where the p95 cannot see it:

| | p50 turnaround | p95 turnaround | p50 queue wait | p95 queue wait |
| --- | --- | --- | --- | --- |
| fifo | 1,258.8s | 2,745.3s | 980.6s | 2,228.2s |
| backfill | 335.7s | 2,711.0s | **23.5s** | 1,967.8s |

Median queue wait drops from about sixteen minutes to twenty-four seconds, with 796 of
2,000 jobs starting ahead of a blocked head. Broken down by job size, small and medium
jobs stop queueing almost entirely, and the large jobs they overtake come out slightly
ahead as well, so no class of job pays for it.

### The utilization ceiling

Pushing the arrival rate up produces a flat line:

| offered rho (cores) | achieved utilization | p95 queue wait |
| --- | --- | --- |
| 0.62 | 0.592 | 2,136s |
| 0.74 | 0.636 | 9,448s |
| 0.92 | 0.638 | 20,839s |
| 1.12 | 0.639 | 29,764s |

Offered load nearly doubles and passes 100% of capacity, queue wait grows fourteenfold,
and utilization settles at 0.64. A third of the cluster sits idle with a large queue
waiting on it.

Queue ordering turns out to have nothing to do with it. Backfill moves the figure from
0.6274 to 0.6319 even while 1,015 of 2,000 jobs jump the queue, and giving the scheduler
perfect walltime knowledge with `--padding 0` changes nothing either.

The limit comes from shape. A `cache` job takes 75% of a node's memory and 25% of its
cores, while a `large` job needs all 16 cores. They cannot share a node, so free
capacity ends up in pieces too small for the jobs that want them. Utilization is work
divided by elapsed time, and the arithmetic falls out exactly:
`1.1176 x 44,444s / 77,944s = 0.6373`. The same work packed perfectly would need 49,671
capacity-seconds, against the 77,944s the cluster actually takes.

So backfill improves how long jobs wait without changing how much work the cluster gets
through. Raising the ceiling is a question about node sizes and job mix, which makes it
a workload and topology problem instead of a scheduling one.

## Architecture

```
              Python  ──  experiments, analysis, dashboard        (S6)
                 │
                 │  pybind11
                 ▼
              libatlas  (C++20)
                 │
     engine/  clock, ids, events, event queue, simulator
     model/   resources, nodes, jobs, cluster, random, workload
     metrics/ load factor, utilization integral, percentiles
     io/      CLI options, JSONL trace, run report
     net/     topology, flows, bandwidth sharing                  (S5)
     sched/   placement policies + queue policies (backfill)
```

`libatlas` is a static library and `src/main.cpp` is a thin CLI on top of it. Everything
lives in a flat `namespace atlas`, and includes are written from `src/` down so a
header's path names its module:

```cpp
#include "atlas/engine/event_queue.hpp"
```

Directories marked with a stage do not exist yet.

[![architecture](docs/architecture.svg)](docs/architecture.svg)

The diagram shows who owns what and how work moves through a run, including the two
scheduling interfaces and what each layer is allowed to see. Open the image for a
readable copy. [`docs/job-lifecycle.svg`](docs/job-lifecycle.svg) is a companion view of
a single job's states. Both render from the `.dot` sources next to them using
`dot -Tsvg`.

## Trace format

`--trace PATH` writes a JSONL file, one JSON object per line, so it streams into pandas
and diffs cleanly:

```jsonl
{"ev":"header","seed":7,"nodes":8,"jobs":2000,"arrival_rate":0.025,"scheduler":"first_fit","queue":"fifo","padding":2}
{"ev":"job","job":0,"submit":85681869,"dur":1560874,"est":3092048,"cores":1,"mem_mb":2048}
{"t":85681869,"seq":0,"ev":"JobArrival","job":0}
{"t":85681869,"ev":"JobStart","job":0,"node":0}
{"t":87242743,"seq":2000,"ev":"JobFinish","job":0,"node":0}
```

All times are integer microseconds, never floating point. There is one `job` record per
job holding the static workload, including `est`, the walltime the user claimed, kept
separate from `dur` so an analysis can measure how wrong the claim was. The timestamped
records are what actually happened. `JobStart` carries no `seq` because placement never
becomes an event; it is decided inside an event handler, so file order is what separates
records sharing a timestamp.

The same seed produces a byte-identical trace on any compiler at any optimization level.
One run's complete trace is checked into `tests/golden/`, and the `golden_trace` test
regenerates it and compares byte for byte:

```bash
make determinism   # the golden trace under debug, release and asan
```

CI runs the same comparison across gcc and clang at `-O0` and `-O3`, so a trace that
shifts under any of them fails the build. This is what makes a result here reproducible
instead of anecdotal. When a change to the trace is intended, `make golden-update`
regenerates the file, after reading the diff.

## Roadmap

- [x] **S1** Vertical slice: event queue, clock, nodes, jobs, first-fit placement
- [x] **S2** CMake, GoogleTest, CI, golden-trace determinism test
- [x] **S3** Pluggable scheduler + queue policies (FIFO, EASY backfill) + metrics
- [ ] **S4** Live terminal dashboard (FTXUI)
- [ ] **S5** Network fabric with max-min fair bandwidth sharing
- [ ] **S6** YAML config, pybind11 bindings, Python experiment harness
- [ ] **S7** DAG workloads, services, failure injection, migration
- [ ] **S8** Web dashboard, Docker, release

## License

Not yet chosen.
