# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Read this first

**This repo is still a single translation unit.** There are no headers yet, so rules that
would normally live next to the code they govern are written down here instead.

- `README.md` — what Atlas is, planned architecture, stage roadmap
- This file — current state, settled decisions, and design invariants

As real code lands, sections here should *shrink*: once `clock.hpp` exists, the invariant
about integer time belongs in `clock.hpp`, not in a markdown file.

## Current state

- **Stage:** S1 complete (steps 1.1-1.5). S2 is next.
- **What exists:** everything is in `src/atlas.cpp` (~1,520 lines, one translation
  unit) — `Tick` and strong `JobId`/`NodeId`; variant event payloads and
  `EventQueue`; `Resources`/`Node`/`Job`/`Cluster` with first-fit placement;
  `RandomSource`, `WorkloadGenerator`, and `offered_load`; `Simulator`, which runs
  them; and the reporting layer from 1.5 — `UtilizationIntegral`, `percentiles`,
  `turnaround_times`/`queue_wait_times`, `TraceWriter`, `Options`/`parse_args`,
  and `report`. Built with `make`. CMake replaces the Makefile in S2.
- **A run reports.** `SimEnd` is scheduled at `now_` once the queue drains, and its
  handler closes the utilization integral — which is what gives the integral a
  definite upper limit. `atlas` with no arguments runs the self-checks; with any
  argument it runs one simulation and prints offered rho, achieved utilization,
  turnaround and queue-wait percentiles, and the censored counts.
- **Placement is strict FIFO.** `drain()` stops at the head of `pending_` when it
  does not fit, so a large job blocks smaller ones behind it. At the default
  operating point this costs nothing at the median — p50 queue wait is 0s — and
  everything in the tail, where p95 is roughly ten minutes. Backfill and pluggable
  schedulers are S3, and this is the baseline they get measured against.
- **Censored jobs are counted, never silently dropped.** A job that never finished
  is excluded from the turnaround sample and counted in `TimeSample::skipped`. A
  large `skipped` beside a near-zero utilization is the signature of a cluster that
  cannot fit part of its own workload, and it is how the node/profile capacity
  mismatch in step 1.5 was caught.
- **Self-checks:** six `check_*()` functions called from `main`, which exits
  non-zero on failure. They stand in for real tests until GoogleTest arrives in S2.
  Each new step adds one; earlier ones stay as regression guards. The seventh
  acceptance check compares two binaries at once, so it cannot be one of them and
  lives in the Makefile as `check-determinism`.
- **Operating point:** workload constants are deliberately tuned so that offered load is
  about rho 0.67 on cores and 0.34 on memory. At rho >= 1 the queue grows without bound
  and every downstream metric becomes meaningless, so re-check `offered_load` after
  changing the arrival rate, the profile table, or the cluster size. Node capacity
  (`kDefaultResources`) and the profile table have to stay compatible: a node smaller
  than the largest profile can never place it, and FIFO then wedges the whole queue.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.

### Next: S2, CMake and real tests

Split the single translation unit into headers and sources, replace the Makefile with
CMake + `FetchContent`, move the six `check_*()` functions into GoogleTest cases, turn
`check-determinism` into a golden-trace test, and make the warning set an error in CI.
As headers appear, the invariants below move into the ones they govern and this file
gets shorter.

## Architecture decisions (settled)

- **C++20.** g++ 13 has solid C++20; C++23 only partially (`std::expected` is available).
- **C++ core → pybind11 → Python** for experiments, analysis, and dashboard. Keep
  JavaScript/TypeScript to a minimum.
- **Visualization:** FTXUI terminal dashboard in S4 (100% C++); Plotly Dash web UI in S8.
- **Dependencies:** CMake `FetchContent` — GoogleTest, yaml-cpp, nlohmann/json, pybind11.

## Design invariants

Violating any of these silently invalidates every experimental result the project
produces. Most now have a home in code; the anchor is where the reasoning lives, and
it should move into the relevant header as S2 splits the file.

1. **Simulated time is `std::uint64_t` microseconds** — `using Tick`. Never floating
   point: FP accumulates drift, makes equality unreliable, and varies across
   optimization levels.
2. **Events are ordered by `(time, seq)`** — `EarliestFirst` and `EventQueue`, which
   owns the counter so that no caller can forget to stamp one.
3. **Events never hold pointers or references into mutable state** — IDs only, at the
   payload definitions. `pending_` follows the same rule for the backlog, because
   `jobs_` is a vector and reallocation would invalidate every pointer at once.
4. **Determinism is the headline property.** Same seed → byte-identical trace, across
   `-O0` and `-O2`, enforced by `make check-determinism`. This one cannot be expressed
   in code, so the watch-list stays here: `unordered_map` iteration order, address-based
   sorting, unstable `std::sort`, stdlib-dependent random distributions (`RandomSource`
   hand-rolls its transforms for exactly this reason), and floating point anywhere
   except the final human-readable report.
5. **Utilization is a time-weighted integral**, never an average of event-time samples
   — `UtilizationIntegral`, advanced at every event *before* the handler changes state.
   Events are not evenly spaced, so sampling weights a microsecond as heavily as a
   minute.
6. **Events use `std::variant` + `std::visit`; schedulers use virtual dispatch.**
   Deliberate and opposite: the event type set is closed with many operations over it,
   while the scheduler set is open with exactly one operation. Half of this is in code
   (`EventPayload`); the scheduler half arrives in S3.
7. **Percentiles use the nearest-rank convention** — `percentiles()`. Changing it later
   silently invalidates every cross-scheduler comparison made before the change.
8. **The trace records observed facts, not derived ones.** Placement is logged where it
   happens (`TraceWriter::placement`, called from `drain()`) rather than reconstructed
   as finish minus duration, so a golden trace can catch a placement bug instead of
   restating the assumption under test. Trace timestamps stay integer ticks; seconds
   appear only in `report`.

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
make                    # build ./atlas
make run                # build, then run the six self-checks
make sim                # build, then run one simulation and print the report;
                        # override any parameter: make sim NODES=16 RATE=0.035
make check-determinism  # identical trace across -O0 and -O2
make clean

# From S2, once CMake replaces the Makefile:
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```
