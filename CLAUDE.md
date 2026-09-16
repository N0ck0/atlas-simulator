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

- **Stage:** S1 in progress. Step 1.1 (time, event types, `EventQueue`) done and
  passing its self-check. Next: 1.2 cluster model, 1.3 workload generation,
  1.4 simulator loop, 1.5 metrics.
- **On disk:** `README.md`, `.gitignore`, `CLAUDE.md`, `src/atlas.cpp`.
- **Toolchain:** g++ 13.3, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1, clang-format 18.1.3,
  Python 3.12. WSL2 / Ubuntu 24.04.

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
