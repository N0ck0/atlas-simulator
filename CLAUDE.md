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

- **Stage:** S1 complete (steps 1.1-1.5). S2 in progress: 2.1 (CMake), 2.2
  (clang-format), 2.3 (header split), 2.4 (GoogleTest) and 2.5 (tests ported) done.
- **What exists:** `libatlas`, a static library of 25 headers and 8 sources under
  `src/atlas/`, plus a thin CLI. Everything is in a flat `namespace atlas`, and
  includes are written from `src/` down (`#include "atlas/engine/clock.hpp"`).
  - `engine/` — `clock` (`Tick`, `kNever`), `ids`, `event`, `event_queue`, `simulator`
  - `model/` — `resources`, `node`, `job`, `cluster` (first-fit), `random`,
    `profiles` (the operating point), `workload`
  - `metrics/` — `load_factor`, `time_sample`, `percentiles`, `utilization`,
    `offered_load`
  - `io/` — `options` (CLI parsing), `trace`, `report`
  - `src/main.cpp` is the CLI, and the only source outside the library.
- **A run reports.** `SimEnd` is scheduled at `now_` once the queue drains, and its
  handler closes the utilization integral — which is what gives the integral a
  definite upper limit. `atlas` runs one simulation and prints offered rho,
  achieved utilization, turnaround and queue-wait percentiles, and the censored
  counts; every option has a default, so a bare `atlas` is a valid run.
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
- **Tests:** GoogleTest via `FetchContent`, pinned to v1.15.2, in `tests/`
  mirroring `src/`. One binary, `atlas_tests`; `gtest_discover_tests` registers
  each case with ctest individually, so `ctest -R` and `ctest -j` work.
- **Correctness lives in `tests/`.** The six `check_*()` functions that stood in
  for tests through S1 are gone; their claims are 39 GoogleTest cases. The
  determinism acceptance check still cannot be one of them — it needs two
  binaries at once — and stays in `cmake/CheckDeterminism.cmake` until 2.6.
  Each new step adds one; earlier ones stay as regression guards. The seventh
  acceptance check compares two binaries at once, so it cannot be one of them and
  lives in `cmake/CheckDeterminism.cmake`, driven by the `check-determinism` target.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.

### Next: S2, CMake and real tests

CMake and presets (2.1), `.clang-format` (2.2), the header split (2.3) and GoogleTest
(2.4) and the ported test suite (2.5) are in place. Still ahead: turning
`check-determinism` into a golden-trace test (2.6), and GitHub Actions with the warning
set as an error (2.7).

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
| `std::variant` for events, virtual dispatch for schedulers | `engine/event.hpp` (scheduler half arrives in S3) |

**Determinism is the headline property**, and the one invariant with no home in code.
Same seed → byte-identical trace, across `-O0` and `-O2`, enforced by the
`check-determinism` target. So the watch-list stays here: `unordered_map` iteration
order, address-based sorting, unstable `std::sort`, stdlib-dependent random
distributions (`RandomSource` hand-rolls its transforms for exactly this reason), and
floating point anywhere except the final human-readable report.

**The operating point** is a property of the numbers, not of any one file. Workload
constants are deliberately tuned so that offered load is about rho 0.67 on cores and
0.34 on memory. At rho >= 1 the queue grows without bound and every downstream metric
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
