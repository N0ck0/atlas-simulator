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

- **Stage:** S0 complete (toolchain, git, remote). S1 not started.
- **On disk:** `README.md`, `.gitignore`, `CLAUDE.md`. No source code yet.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.
- **Next:** S1 vertical slice — event queue, simulated clock, nodes, jobs, first-fit
  placement, metrics. One translation unit, built with a bare `g++` call. CMake lands in S2.

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

- **Every declaration in a header gets a comment** covering what it does, plus any
  precondition, ownership, or lifetime requirement. Preconditions matter most here —
  "caller must have already verified capacity" is exactly the kind of unwritten
  assumption that becomes a bug.
- **Definitions get comments only where the *how* is non-obvious**: a non-local ordering
  requirement, an algorithm choice with a rejected alternative, a deliberate workaround.
- **Don't comment trivial accessors.** `// returns the size` above `size()` is noise, and
  it trains readers to skip comments — including the one that mattered.
- Prefer explaining *why* over *what*. The code already says what.

## Commands

Nothing to build yet.

```bash
# From S1 (single translation unit):
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -o atlas src/atlas.cpp && ./atlas --seed 42

# From S2 (CMake):
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```
