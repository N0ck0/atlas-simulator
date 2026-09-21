# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Read this first

**This repo is nearly empty and will stay that way for a while.** The code does not yet
describe the project, so the rules it would normally encode are written down here instead.

- `README.md` — what Atlas is, planned architecture, stage roadmap
- This file — current state, settled decisions, and design invariants

As real code lands, sections here should *shrink*: once `clock.hpp` exists, the invariant
about integer time belongs in `clock.hpp`, not in a markdown file.

## Current state

<!-- UPDATE THIS AT THE END OF EVERY SESSION -->

- **Stage:** S1 (vertical slice). Steps 1.1-1.4 complete.
- **What exists:** everything is in `src/atlas.cpp` (~980 lines, one translation unit) --
  `Tick` and strong `JobId`/`NodeId`; variant event payloads and `EventQueue`;
  `Resources`/`Node`/`Job`/`Cluster` with first-fit placement; `RandomSource`;
  `WorkloadGenerator` and `offered_load`; and `Simulator`, which runs them. Built with
  `make` (`make run` to build and run). CMake replaces the Makefile in S2.
- **The loop runs end to end.** `Simulator` seeds one `JobArrival` per job, pops until
  the queue is empty, and every job reaches Done with resources returned. Output is
  byte-identical across `-O0` and `-O2`.
- **Placement is strict FIFO and there are no metrics yet.** `drain()` stops at the
  head of `pending_` when it does not fit, so a large job blocks smaller ones behind
  it; backfill and pluggable schedulers are S3. `SimEnd` is defined and dispatched but
  never scheduled -- it gets a real time in 1.5, when it bounds the utilization
  integral.
- **Self-checks:** five `check_*()` functions called from `main`, which exits non-zero on
  failure. They stand in for real tests until GoogleTest arrives in S2. Each new step
  adds one; earlier ones stay as regression guards.
- **Operating point:** workload constants are deliberately tuned so that offered load is
  about rho 0.67 on cores and 0.34 on memory. At rho >= 1 the queue grows without bound
  and every downstream metric becomes meaningless, so re-check `offered_load` after
  changing the arrival rate, the profile table, or the cluster size.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.

### Next: step 1.5, metrics and the trace

The last step of the vertical slice. Turns a run that works into a run that reports:

- Time-weighted utilization integrals, accumulated at every event rather than sampled.
- Completion-time percentiles via `nth_element`.
- A JSONL trace with the seed stamped in, and CLI arguments for seed, node count, job
  count, and arrival rate.
- Give `SimEnd` a real time at or after the last finish, so the integrals have a
  definite upper limit.
- Acceptance checks: identical output hash across `-O0` and `-O2` (already holds); more
  nodes lowers mean queue time; pushing rho toward 1 makes queueing explode.

## Architecture decisions (settled)

- **C++20.** g++ 13 has solid C++20; C++23 only partially (`std::expected` is available).
- **C++ core → pybind11 → Python** for experiments, analysis, and dashboard. Keep
  JavaScript/TypeScript to a minimum.
- **Visualization:** FTXUI terminal dashboard in S4 (100% C++); Plotly Dash web UI in S8.
- **Dependencies:** CMake `FetchContent` — GoogleTest, yaml-cpp, nlohmann/json, pybind11.

## Design invariants

Not yet expressed in code, because there is no code. Violating any of these silently
invalidates every experimental result the project produces.

1. **Simulated time is `std::uint64_t` microseconds** (`using Tick = std::uint64_t`).
   Never floating point: FP accumulates drift, makes equality unreliable, and varies
   across optimization levels — which would break reproducibility.
2. **Events are ordered by `(time, seq)`**, where `seq` is a monotonic insertion counter.
   `std::priority_queue` is an unstable heap; without `seq`, equal timestamps pop in
   heap-layout order and the simulation stops being reproducible.
3. **Events never hold pointers or references into mutable state** — IDs only. A pointer
   sitting in the queue for 500 simulated seconds is a dangling-pointer bug waiting to
   happen, and vector reallocation invalidates all of them at once.
4. **Determinism is the headline property.** Same seed → byte-identical event trace,
   across `-O0` and `-O2`. Watch for: `unordered_map` iteration order, address-based
   sorting, unstable `std::sort`, stdlib-dependent random distributions.
5. **Utilization is a time-weighted integral**, never an average of event-time samples.
6. **Events use `std::variant` + `std::visit`; schedulers use virtual dispatch.**
   Deliberate and opposite: the event type set is closed with many operations over it,
   while the scheduler set is open with exactly one operation.

## Conventions

- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`. Errors in CI from S2.
- Strong ID types (`enum class JobId : std::uint32_t`), never bare ints or pointers.
- `PascalCase` types, `snake_case` functions and variables, `trailing_underscore_`
  private members, `kPascalCase` constants.
- `.clang-format` arrives in S2.

### Comments

- Keep them short: a few lines at most, plain prose. No ASCII rules, banners, or
  section dividers.
- Comment only where it earns its place: a design choice and the alternative it
  beat, a non-obvious purpose, or a precondition the caller must satisfy.
- Do not restate the code. No comments on trivial accessors or self-evident names.
- Prefer why over what.

## Commands

```bash
make          # build ./atlas
make run      # build, then run the current step's self-check
make clean

# From S2, once CMake replaces the Makefile:
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```
