# Atlas

A discrete-event simulator for experimentally evaluating datacenter scheduling policies.

> **Status: early development.** Stage 1 is complete — the simulator runs end to end,
> reports metrics, and emits a trace. Stage 2 is in progress. Everything else on the
> roadmap is not implemented; unchecked boxes mean exactly that.

## What this is

You describe a hypothetical datacenter — nodes, racks, network topology — submit a
workload, plug in a scheduling policy, and measure what happens: job completion time,
queueing delay, resource utilization, network congestion.

The point is *not* to run a thousand containers. It is to model what would happen if
you did, fast enough to sweep hundreds of configurations and answer questions like:

> How much does incorporating network topology into scheduling improve job completion
> time under communication-heavy workloads — and what does it cost under CPU-heavy ones?

Simulated time advances by jumping from event to event rather than ticking forward,
so hours of datacenter activity run in seconds of wall-clock time.

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
     sched/   pluggable scheduler interface                       (S3)
```

`libatlas` is a static library; `src/main.cpp` is a thin CLI on top of it. Everything
lives in a flat `namespace atlas`, and includes are written from `src/` down, so a
header's path names its module:

```cpp
#include "atlas/engine/event_queue.hpp"
```

Directories marked with a stage do not exist yet.

## Roadmap

- [x] **S1** Vertical slice — event queue, clock, nodes, jobs, first-fit placement
- [ ] **S2** CMake, GoogleTest, CI, golden-trace determinism test
- [ ] **S3** Pluggable scheduler interface + baselines + metrics
- [ ] **S4** Live terminal dashboard (FTXUI)
- [ ] **S5** Network fabric with max-min fair bandwidth sharing
- [ ] **S6** YAML config, pybind11 bindings, Python experiment harness
- [ ] **S7** DAG workloads, services, failure injection, migration
- [ ] **S8** Web dashboard, Docker, release

## Building and running

Requires a C++20 compiler, CMake 3.25+, and Ninja.

```bash
cmake --preset debug          # also: release, asan
cmake --build --preset debug
./build/debug/atlas           # the self-checks: six suites covering every component
```

There is a `Makefile` for the commands you type most. It is a wrapper — every recipe
forwards to `cmake` or `ctest`, and no build setting lives in it — so the two are
interchangeable:

```bash
make            # build the debug preset
make run        # build, then run the self-checks
make asan       # build with ASan + UBSan
make help       # every target
```

`make sim` runs one simulation with a report, and takes overrides for any parameter, so
an operating point can be swept without a recompile:

```bash
make sim NODES=16 RATE=0.035
```

A run reports what the cluster actually did, next to what was asked of it:

```
run: seed 7  nodes 8  jobs 2000  rate 0.025/s
  span               79950.8s
  offered rho   cores 0.6721  memory 0.3361
  utilization   cores 0.6621  memory 0.3310
  turnaround    p50    140.4s  p95   1335.6s  p99   2375.4s  (0 censored)
  queue wait    p50      0.0s  p95    582.0s  p99    757.5s  (0 censored)
```

`offered rho` is the demand the workload places on the cluster; `utilization` is the
time-weighted fraction actually consumed. They track closely when the cluster keeps
up, and diverge when it cannot. Percentiles rather than means because the
distributions are skewed: at this operating point the median job never queues at
all, while the unlucky one percent waits over twelve minutes.

## Trace format

`--trace PATH` writes a JSONL file — one JSON object per line, so it streams into
pandas and diffs cleanly:

```jsonl
{"ev":"header","seed":7,"nodes":8,"jobs":2000,"arrival_rate":0.025}
{"ev":"job","job":0,"submit":85681869,"dur":1560874,"cores":1,"mem_mb":2048}
{"t":85681869,"seq":0,"ev":"JobArrival","job":0}
{"t":85681869,"ev":"JobStart","job":0,"node":0}
{"t":87242743,"seq":2000,"ev":"JobFinish","job":0,"node":0}
```

Times are integer microseconds, never floating point. A `job` record per job carries
the static workload; the timestamped records are what happened. `JobStart` has no
`seq` because placement is not an event — it is a decision made inside a handler —
so file order, not the `seq` field, orders records sharing a timestamp.

The same seed produces a byte-identical trace regardless of optimization level, which
the `check-determinism` target verifies by building at `-O0` and `-O2` and comparing:

```bash
make determinism
# or: cmake --build --preset debug --target check-determinism
```

That property is what makes an experimental result reproducible rather than anecdotal,
and it becomes a golden-trace test later in S2.

## License

Not yet chosen.
