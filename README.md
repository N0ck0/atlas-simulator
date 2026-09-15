# Atlas

A discrete-event simulator for experimentally evaluating datacenter scheduling policies.

> **Status: early development.** Scaffolding only — there is no working simulator yet.
> The roadmap below tracks real progress; unchecked boxes are not implemented.

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

## Planned architecture

```
              Python  ──  experiments, analysis, dashboard
                 │
                 │  pybind11
                 ▼
              libatlas  (C++20)
                 │
     engine/  event queue, simulated clock
     model/   nodes, racks, jobs
     net/     topology, flows, bandwidth sharing
     sched/   pluggable scheduler interface
     metrics/ utilization, latency percentiles
```

## Roadmap

- [ ] **S1** Vertical slice — event queue, clock, nodes, jobs, first-fit placement
- [ ] **S2** CMake, GoogleTest, CI, golden-trace determinism test
- [ ] **S3** Pluggable scheduler interface + baselines + metrics
- [ ] **S4** Live terminal dashboard (FTXUI)
- [ ] **S5** Network fabric with max-min fair bandwidth sharing
- [ ] **S6** YAML config, pybind11 bindings, Python experiment harness
- [ ] **S7** DAG workloads, services, failure injection, migration
- [ ] **S8** Web dashboard, Docker, release

## Building

Nothing to build yet. Requirements so far: a C++20 compiler, CMake 3.28+, Ninja.

## License

Not yet chosen.
