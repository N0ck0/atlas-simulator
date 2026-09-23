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

- **Stage:** S1 complete (steps 1.1-1.5). S2 in progress: 2.1 (CMake) and 2.2
  (clang-format) done.
- **What exists:** everything is in `src/atlas.cpp` (~1,520 lines, one translation
  unit) — `Tick` and strong `JobId`/`NodeId`; variant event payloads and
  `EventQueue`; `Resources`/`Node`/`Job`/`Cluster` with first-fit placement;
  `RandomSource`, `WorkloadGenerator`, and `offered_load`; `Simulator`, which runs
  them; and the reporting layer from 1.5 — `UtilizationIntegral`, `percentiles`,
  `turnaround_times`/`queue_wait_times`, `TraceWriter`, `Options`/`parse_args`,
  and `report`. Built with CMake + presets, with a `Makefile` wrapper for the common
  commands; the split into headers is still ahead.
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
  lives in `cmake/CheckDeterminism.cmake`, driven by the `check-determinism` target.
- **Operating point:** workload constants are deliberately tuned so that offered load is
  about rho 0.67 on cores and 0.34 on memory. At rho >= 1 the queue grows without bound
  and every downstream metric becomes meaningless, so re-check `offered_load` after
  changing the arrival rate, the profile table, or the cluster size. Node capacity
  (`kDefaultResources`) and the profile table have to stay compatible: a node smaller
  than the largest profile can never place it, and FIFO then wedges the whole queue.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.

### Next: S2, CMake and real tests

CMake and presets (2.1) and `.clang-format` (2.2) are in place. Still ahead: splitting the
single translation unit into fine-grained headers under a flat `namespace atlas` with a
`libatlas` static library and a thin CLI (2.3), GoogleTest via `FetchContent` (2.4), moving
the six `check_*()` functions into test cases (2.5), turning `check-determinism` into a
golden-trace test (2.6), and GitHub Actions with the warning set as an error (2.7).
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
   `-O0` and `-O2`, enforced by the `check-determinism` target. This one cannot be expressed
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
make run                # build, then run the six self-checks
make run-asan           # the same self-checks under the sanitizers
make sim                # build, then run one simulation and report;
                        # override any parameter: make sim NODES=16 RATE=0.035
make test               # ctest; registers no cases until step 2.4
make determinism        # identical trace across -O0 and -O2
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
cmake --build --preset debug --target check-determinism

./build/debug/atlas             # no arguments: the six self-checks
./build/debug/atlas --seed 7 --nodes 8 --jobs 2000 --rate 0.025 --trace trace.jsonl
```

Presets write to `build/<preset>/`, so `rm -rf build` is always a safe reset. Each build
target depends on its preset's `CMakeCache.txt`, which makes the configure step run once
per build directory rather than on every build.

Presets write to `build/<preset>/`. `asan` is Debug plus ASan/UBSan with
`-fno-sanitize-recover=all`, so a violation aborts instead of printing and continuing.
`-Werror` is off by default and set by CI via `-DATLAS_WERROR=ON`: a warning should stop
a merge, not a local experiment.
