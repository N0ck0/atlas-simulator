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

- **Stage:** S1, S2 and S3 complete. 3.1-3.5: the Scheduler interface, the
  factory and `--scheduler`, four placement policies, the comparison table, and
  the QueuePolicy interface with `fifo` and EASY `backfill`.
- **What exists:** `libatlas`, a static library of 32 headers and 17 sources under
  `src/atlas/`, plus a thin CLI. Everything is in a flat `namespace atlas`, and
  includes are written from `src/` down (`#include "atlas/engine/clock.hpp"`).
  - `engine/` — `clock` (`Tick`, `kNever`), `ids`, `event`, `event_queue`, `simulator`
  - `model/` — `resources`, `node`, `job`, `cluster` (first-fit), `random`,
    `profiles` (the operating point), `workload`
  - `metrics/` — `load_factor`, `time_sample`, `percentiles`, `utilization`,
    `offered_load`
  - `io/` — `options` (CLI parsing), `trace`, `report`, `compare` (the sweep)
  - `sched/` — two orthogonal interfaces. `scheduler` (which node) with
    `factory`, `dominant_share` and four policies: `first_fit`, `round_robin`,
    `least_loaded`, `resource_aware`. `queue_policy` (which job) with
    `queue_factory`, `fifo_queue` and `backfill`.
  - `src/main.cpp` is the CLI, and the only source outside the library.
- **A run reports.** `SimEnd` is scheduled at `now_` once the queue drains, and its
  handler closes the utilization integral — which is what gives the integral a
  definite upper limit. `atlas` runs one simulation and prints offered rho,
  achieved utilization, turnaround and queue-wait percentiles, and the censored
  counts; every option has a default, so a bare `atlas` is a valid run.
  `atlas --compare` instead runs every scheduler over `--seeds` consecutive seeds
  and prints the table. It rejects `--trace`, which a sweep would write once per
  run over a single path.
- **`drain()` has two phases.** Phase 1 is strict FIFO from the head until
  something does not fit — the entire behaviour when the queue policy is `fifo`,
  and byte-identical to the pre-3.5 simulator. Phase 2 asks the QueuePolicy what
  may jump the blocked head. It terminates because every iteration removes
  exactly one job from `pending_`; there is no scan-to-exhaustion state.
- **The 64% utilization ceiling is a packing limit, not a queueing one.** This
  was misdiagnosed once, so the evidence is written down. Utilization flattens at
  ~0.63 as offered rho rises past 1.0, which looks like FIFO head-of-line
  blocking. It is not: `backfill` removes that blocking — 1,015 of 2,000 jobs
  jump the queue at rho 1.12 — and the ceiling moves only 0.6274 to 0.6319, span
  77,944s to 77,390s. `--padding 0`, which hands the scheduler perfect walltime
  knowledge, changes nothing either. Utilization is work over span, and the
  identity `0.6373 = 1.1176 x 44,444 / 77,944` reproduces the measured number:
  the same work perfectly packed needs 49,671 capacity-seconds against 77,944s
  of wall time. The gap is multi-dimensional fragmentation — `cache` takes 75% of
  a node's memory and 25% of its cores, `large` needs 100% of the cores, and the
  two cannot share a node. No queue ordering recovers it; only a different node
  size or job mix would.
- **Censored jobs are counted, never silently dropped.** A job that never finished
  is excluded from the turnaround sample and counted in `TimeSample::skipped`. A
  large `skipped` beside a near-zero utilization is the signature of a cluster that
  cannot fit part of its own workload, and it is how the node/profile capacity
  mismatch in step 1.5 was caught.
- **Tests:** 103 cases under GoogleTest via `FetchContent`, pinned to v1.15.2, in `tests/`
  mirroring `src/`. One binary, `atlas_tests`; `gtest_discover_tests` registers
  each case with ctest individually, so `ctest -R` and `ctest -j` work.
- **Correctness lives in `tests/`.** The six `check_*()` functions that stood in
  for tests through S1 are gone; their claims are GoogleTest cases now, plus the
  `golden_trace` case that pins one run's entire output byte for byte.
- **CI is GitHub Actions**, `.github/workflows/ci.yml`: gcc 13 and clang 18 against
  debug and release, a sanitizer job, and a `clang-format` check, all with
  `-DATLAS_WERROR=ON`. CI invokes `cmake` and `ctest` directly, never the
  `Makefile`, so the wrapper can never become load-bearing.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.

### What S3 measured

A single seed suggested the four policies separated by about 2% on p95 turnaround in
the direction theory predicts. Thirty seeds say otherwise. Because every scheduler runs
every seed, the table reports the *paired* difference, which cancels the seed's own
difficulty -- the term that dominates the unpaired sd and hides everything else. The
best policy is 12.8s +- 7.3s ahead of the `first_fit` baseline on a p95 of ~2700s:
under half a percent, and under two standard errors. The policies are not
distinguishable at this operating point.

Keep the two spreads straight, because they answer different questions and differ by
two orders of magnitude on the same data. `sd` (~1200s) is variation across seeds and
is a property of `WorkloadGenerator`. `se` (~8s) is uncertainty on the paired
difference and is the only one that speaks to the scheduler.

The null result is not a defect in the policies. Placement has almost no leverage left
by the time `place()` is called, because FIFO has already chosen which job runs.

**3.5 confirmed that.** Backfill is -96.3s +- 23.6s against the same baseline: four
standard errors, the first effect in this project large enough to claim. The p95 is not
where it lands, though -- median queue wait falls from 980.6s to 23.5s while p95 moves
only 2228.2s to 1967.8s. The jobs that jump are the small ones; the tail belongs to the
`large` jobs that were doing the blocking. That is the tradeoff EASY is supposed to
make, visible directly.

**A prediction that failed, recorded so it is not made twice.** Placement was expected
to start separating once backfill removed the queue bottleneck, on the theory that
packing policies leave larger contiguous gaps. The four backfill rows span 81.7s to
96.3s against standard errors near 23s. Still indistinguishable. Placement in this
model appears not to matter at all, at any load, under either queue policy.

### Why backfill is a second interface and not a fifth policy

Worth keeping, because the reasoning is the answer to "why is `Scheduler` virtual":

- `place(const Job&, std::span<const Node>)` answers "where does *this* job go", and
  `nullopt` means "nowhere, right now". Backfill asks "which job next", a priority
  decision the signature cannot return.
- Looping `place()` over candidates does not work either, and not only for style:
  `place()` is a non-const command, and `RoundRobinScheduler` advances a cursor. A
  speculative call would change the placement decisions of the very policy backfill is
  meant to be orthogonal to. Queue policies use `Node::can_fit`, which is a pure
  predicate.
- Folding both into one interface would multiply names rather than add them: the
  comparison table is a 2x4 product built from 2+4 classes.

### The estimate must not be the truth

`Job::estimated_duration` is the user's walltime claim, always >= `duration`, drawn per
job by `WorkloadGenerator` as a uniform multiple in [1, 1+padding]. `QueuedJob` and
`RunningJob` expose it and omit `duration` entirely, so a policy physically cannot plan
against the service time the simulator will use. Three constraints hold this together:

- **A constant multiplier would not do.** It is invertible -- divide it out and the
  true duration is back -- so it models a known bias, not uncertainty. Dispersion is
  what makes estimated finish order diverge from true finish order, which is the error
  that actually costs anything.
- **The draw happens in the generator, never in the policy.** A draw inside
  `backfill()` would consume a different amount of the random stream depending on how
  often the policy was consulted, so a fifo run and a backfill run would silently be
  running different workloads while each remained individually reproducible.
- **It is a second pass over the finished vector**, not a draw inside the generation
  loop. Interleaving would shift every subsequent draw and change the arrival times and
  durations of existing seeds, making every measurement taken before 3.5 incomparable
  with every one after. The golden trace diff for this change is purely the added
  field; nothing else moved.

Empirically the padding does not matter in this workload -- `--padding 0` and
`--padding 10` give the same utilization and p95 to three figures -- which says the
backfill decisions here are limited by fit, not by estimate quality.

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
floating point anywhere except the final human-readable report. The comparison sweep
follows the same rule: `RunResult` holds `Tick`, so per-seed win counts are integer
comparisons, and the means, standard deviations and standard errors are computed in
`report_comparison` and nowhere earlier.

**The operating point** is a property of the numbers, not of any one file. Workload
constants are deliberately tuned so that offered load is about rho 0.62 on cores and
0.61 on memory -- close together on purpose, because a workload whose dimensions move
together is effectively one-dimensional and no multi-resource policy can differ on it. At rho >= 1 the queue grows without bound and every downstream metric
becomes meaningless, so re-check `offered_load` after changing the arrival rate, the
profile table, or the cluster size. Note that raising the rate does *not* raise
utilization past about 0.64 -- FIFO caps it there -- so rho above ~0.75 buys unbounded
queue growth and no extra work done. Node capacity and the profile table have to stay
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
./build/debug/atlas --queue backfill        # EASY backfill instead of strict FIFO
./build/debug/atlas --compare --seeds 30    # queue x placement, one table
```

Presets write to `build/<preset>/`, so `rm -rf build` is always a safe reset. Each build
target depends on its preset's `CMakeCache.txt`, which makes the configure step run once
per build directory rather than on every build.

Presets write to `build/<preset>/`. `asan` is Debug plus ASan/UBSan with
`-fno-sanitize-recover=all`, so a violation aborts instead of printing and continuing.
`-Werror` is off by default and set by CI via `-DATLAS_WERROR=ON`: a warning should stop
a merge, not a local experiment.
