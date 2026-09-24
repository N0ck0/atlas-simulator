# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Read this first

**Design reasoning lives next to the code it governs.** The headers under `src/atlas/`
carry it; this file holds only what has no home in code.

- `README.md` — what Atlas is, architecture, stage roadmap
- This file — current state, settled decisions, and the invariants code cannot express

When a new invariant turns up, the question is which header owns it. It lands here only
if the answer is none.

## Current state

- **Stage:** S1 and S2 complete. S3 in progress: 3.1-3.3 done — the Scheduler
  interface, the factory and `--scheduler`, and four policies. Next is 3.4, the
  comparison table.
- **What exists:** `libatlas`, a static library of 25 headers and 8 sources under
  `src/atlas/`, plus a thin CLI. Everything is in a flat `namespace atlas`, and
  includes are written from `src/` down (`#include "atlas/engine/clock.hpp"`).
  - `engine/` — `clock` (`Tick`, `kNever`), `ids`, `event`, `event_queue`, `simulator`
  - `model/` — `resources`, `node`, `job`, `cluster` (first-fit), `random`,
    `profiles` (the operating point), `workload`
  - `metrics/` — `load_factor`, `time_sample`, `percentiles`, `utilization`,
    `offered_load`
  - `io/` — `options` (CLI parsing), `trace`, `report`
  - `sched/` — `scheduler` (the abstract interface), `factory`, `dominant_share`,
    and four policies: `first_fit`, `round_robin`, `least_loaded`, `resource_aware`
  - `src/main.cpp` is the CLI, and the only source outside the library.
- **A run reports.** `SimEnd` is scheduled at `now_` once the queue drains, and its
  handler closes the utilization integral — which is what gives the integral a
  definite upper limit. `atlas` runs one simulation and prints offered rho,
  achieved utilization, turnaround and queue-wait percentiles, and the censored
  counts; every option has a default, so a bare `atlas` is a valid run.
- **Placement is strict FIFO.** `drain()` stops at the head of `pending_` when it
  does not fit, so a large job blocks smaller ones behind it. At the default
  operating point every job waits: p50 queue wait is about 16 minutes against a
  p50 turnaround of 21. That head-of-line blocking dominates, which is why the
  four schedulers in S3 separate by only a couple of percent — where a job lands
  matters far less than the fact that a `large` job ahead of it holds the whole
  queue. Backfill is not implemented; this FIFO baseline is what it would be
  measured against.
- **Censored jobs are counted, never silently dropped.** A job that never finished
  is excluded from the turnaround sample and counted in `TimeSample::skipped`. A
  large `skipped` beside a near-zero utilization is the signature of a cluster that
  cannot fit part of its own workload, and it is how the node/profile capacity
  mismatch in step 1.5 was caught.
- **Tests:** GoogleTest via `FetchContent`, pinned to v1.15.2, in `tests/`
  mirroring `src/`. One binary, `atlas_tests`; `gtest_discover_tests` registers
  each case with ctest individually, so `ctest -R` and `ctest -j` work.
- **Correctness lives in `tests/`.** The six `check_*()` functions that stood in
  for tests through S1 are gone; their claims are 39 GoogleTest cases, plus the
  `golden_trace` case that pins one run's entire output byte for byte.
- **CI is GitHub Actions**, `.github/workflows/ci.yml`: gcc 13 and clang 18 against
  debug and release, a sanitizer job, and a `clang-format` check, all with
  `-DATLAS_WERROR=ON`. CI invokes `cmake` and `ctest` directly, never the
  `Makefile`, so the wrapper can never become load-bearing.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.

### Next: 3.4, the comparison table

Four policies exist and separate in the direction theory predicts -- packing
(`resource_aware`, `first_fit`) beats spreading (`least_loaded`, `round_robin`) because
a `large` job needs a whole node and spreading fragments capacity. The margin is about
2% on p95 turnaround, which is thin for a headline result: strict FIFO means the queue
discipline dominates the placement decision. 3.4 renders the table; 3.5 decides whether
to widen the gap (backfill, heterogeneous nodes, or a load band where the queue is
active but not saturated) or to report the narrow result and explain it.

## Architecture decisions (settled)

- **C++20.** g++ 13 has solid C++20; C++23 only partially (`std::expected` is available).
- **C++ core → pybind11 → Python** for experiments, analysis, and dashboard. Keep
  JavaScript/TypeScript to a minimum.
- **Visualization:** FTXUI terminal dashboard in S4 (100% C++); Plotly Dash web UI in S8.
- **Dependencies:** CMake `FetchContent` — GoogleTest, yaml-cpp, nlohmann/json, pybind11.

## Design invariants

Violating any of these silently invalidates every experimental result the project
produces. Each one that has a home in code now lives there, in the header named
below; only what code cannot express is written out here.

| Invariant | Where it lives |
| --- | --- |
| Simulated time is `std::uint64_t` microseconds, never floating point | `engine/clock.hpp` |
| Events are ordered by `(time, seq)`, and the queue owns the counter | `engine/event_queue.hpp` |
| Events and the backlog hold IDs, never pointers into mutable state | `engine/event.hpp`, `engine/simulator.hpp` |
| Utilization is a time-weighted integral, never an average of samples | `metrics/utilization.hpp` |
| Percentiles use the nearest-rank convention | `metrics/percentiles.hpp` |
| The trace records observed facts, not derived ones | `io/trace.hpp` |
| `std::variant` for events, virtual dispatch for schedulers | `engine/event.hpp`, `sched/scheduler.hpp` |

**Determinism is the headline property**, and the one invariant with no home in code.
Same seed → byte-identical trace, across compilers and optimization levels. The
`golden_trace` test enforces it for one build; CI enforces it across all of them by
running that same test under gcc and clang at `-O0` and `-O3`. What no test can
enforce is not writing the bug in the first place, so the watch-list stays here: `unordered_map` iteration
order, address-based sorting, unstable `std::sort`, stdlib-dependent random
distributions (`RandomSource` hand-rolls its transforms for exactly this reason), and
floating point anywhere except the final human-readable report.

**The operating point** is a property of the numbers, not of any one file. Workload
constants are deliberately tuned so that offered load is about rho 0.62 on cores and
0.61 on memory -- close together on purpose, because a workload whose dimensions move
together is effectively one-dimensional and no multi-resource policy can differ on it. At rho >= 1 the queue grows without bound and every downstream metric
becomes meaningless, so re-check `offered_load` after changing the arrival rate, the
profile table, or the cluster size. Node capacity and the profile table have to stay
compatible — a node smaller than the largest profile can never place it, and FIFO then
wedges the whole queue — which is why `kDefaultResources` sits beside `kJobProfiles` in
`model/profiles.hpp`.

## Conventions

- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`. Errors in CI from S2.
- Strong ID types (`enum class JobId : std::uint32_t`), never bare ints or pointers.
- `PascalCase` types, `snake_case` functions and variables, `trailing_underscore_`
  private members, `kPascalCase` constants.
- `.clang-format` is Google style with four deviations: 4-space indent, 100-column
  limit, left-aligned `*` and `&`, and no comment reflow. `make fmt` formats the tree,
  `make fmt-check` reports without editing. clang-format output differs between major
  versions, so CI pins 18 to match local.

### Comments

These govern C++ source. Build files are the exception: `CMakeLists.txt`, the presets,
and the `Makefile` carry longer explanatory comments on purpose, because they are the
only documentation of a toolchain nobody reads daily.

- Keep them short: a few lines at most, plain prose. No ASCII rules, banners, or
  section dividers.
- Comment only where it earns its place: a design choice and the alternative it
  beat, a non-obvious purpose, or a precondition the caller must satisfy.
- Do not restate the code. No comments on trivial accessors or self-evident names.
- Prefer why over what.

### Git Usage

- Do not stage/commit changes without specific user direction to do so

## Commands

CMake is the build system. The `Makefile` is a wrapper over it and contains no build
knowledge of its own — no flags, no source lists, every recipe a single forward to
`cmake` or `ctest`. Keep it that way: a flag that lives in two places is a flag that
will disagree with itself. CI invokes `cmake` directly so the wrapper can never become
load-bearing.

```bash
make                    # build the debug preset (the default target)
make release            # build optimized
make asan               # build with ASan + UBSan
make sim                # build, then run one simulation and report;
                        # override any parameter: make sim NODES=16 RATE=0.035
make test               # run the test suite through ctest
make test-asan          # the same suite under ASan + UBSan
make determinism        # golden trace identical across all three presets
make golden-update      # regenerate the golden trace (read the diff first)
make fmt                # reformat all sources with clang-format
make fmt-check          # report unformatted files without editing
make clean              # delete build/ and trace.jsonl
make help               # the above, from the shell
```

The underlying commands, which CI uses and which the wrapper only shortens:

```bash
cmake --preset debug            # configure; also `release` and `asan`
cmake --build --preset debug    # build build/debug/atlas
ctest --preset debug

./build/debug/atlas             # one simulation at the default operating point
./build/debug/atlas --seed 7 --nodes 8 --jobs 2000 --rate 0.025 --trace trace.jsonl
```

Presets write to `build/<preset>/`, so `rm -rf build` is always a safe reset. Each build
target depends on its preset's `CMakeCache.txt`, which makes the configure step run once
per build directory rather than on every build.

Presets write to `build/<preset>/`. `asan` is Debug plus ASan/UBSan with
`-fno-sanitize-recover=all`, so a violation aborts instead of printing and continuing.
`-Werror` is off by default and set by CI via `-DATLAS_WERROR=ON`: a warning should stop
a merge, not a local experiment.
