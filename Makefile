# Convenience wrapper around CMake, which is the real build system. Every recipe
# here forwards to `cmake` or `ctest` and holds no build knowledge of its own: no
# compiler flags, no source lists, no include paths. A setting that lives in two
# places is a setting that will eventually disagree with itself, so flags belong
# in CMakeLists.txt or CMakePresets.json. CI calls cmake directly, so nothing
# here is load-bearing.

# Bare `make` builds the debug preset, the thing done most often.
.DEFAULT_GOAL := debug

# Configures a build directory: `cmake --preset X` reads CMakePresets.json and
# generates build/X/. Runs only when that directory has no CMakeCache.txt yet,
# so the ~2s configure cost is paid once per build directory rather than on
# every build. Ninja re-runs CMake itself when CMakeLists.txt or
# CMakePresets.json change, so staleness needs no handling here.
build/%/build.ninja:
	cmake --preset $*

# Compiles build/debug/atlas at -O0 -g with assertions live. Ninja works out
# what changed and parallelizes across cores, so there is no -j to pass.
debug: build/debug/build.ninja
	cmake --build --preset debug

# Compiles build/release/atlas at -O3 with NDEBUG set, which disables assert().
release: build/release/build.ninja
	cmake --build --preset release

# Compiles build/asan/atlas with AddressSanitizer and UndefinedBehaviorSanitizer
# linked in: a few times slower, and aborts with a stack trace the moment the
# program reads out of bounds, uses freed memory, or overflows a signed integer.
asan: build/asan/build.ninja
	cmake --build --preset asan

# Runs one simulation and prints the report, which is all a bare `atlas` does.
# Every parameter is overridable on the command line, so an operating point can
# be swept without editing anything:
#   make sim NODES=16 RATE=0.035
SEED  ?= 7
NODES ?= 8
JOBS  ?= 2000
RATE  ?= 0.025
TRACE ?= trace.jsonl

sim: debug
	./build/debug/atlas --seed $(SEED) --nodes $(NODES) --jobs $(JOBS) \
	                    --rate $(RATE) --trace $(TRACE)

# Runs the GoogleTest suite through ctest, which reports pass/fail per case.
# ctest rather than the test binary directly because it is the layer that also
# runs non-GoogleTest checks -- the golden-trace comparison arriving in 2.6.
test: debug
	ctest --preset debug

# The same suite under ASan and UBSan. Slower, and the only place a memory bug
# in the code under test will actually announce itself.
test-asan: asan
	ctest --preset asan

# Invariant 4: the same seed must produce a byte-identical trace regardless of
# optimization level. The golden_trace ctest case pins the trace for one build;
# running it under all three presets is what pins it ACROSS builds, since debug
# is -O0, release is -O3 and asan adds instrumentation on top. CI does the same
# thing by running ctest in each configuration.
determinism: debug release asan
	ctest --preset debug -R golden_trace
	ctest --preset release -R golden_trace
	ctest --preset asan -R golden_trace

# Regenerates the checked-in golden trace. Only ever run this after reading the
# diff and concluding the change was intended: blessing an unexamined diff
# discards exactly the regression the golden trace exists to catch.
golden-update: debug
	./build/debug/atlas --seed 7 --nodes 4 --jobs 40 --rate 0.05 \
	                   --trace tests/golden/small.jsonl

# Every C++ source git knows about, tracked or not. --others --exclude-standard
# adds files that are new but not gitignored: without them a brand-new file is
# silently skipped, and a formatting gate that passes because it checked nothing
# is worse than no gate at all. -r stops xargs running clang-format with no
# arguments, which would make it wait on stdin.
ATLAS_SOURCES = git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp'

# Reformats every source in place using the rules in .clang-format. Safe to run
# at any time: clang-format changes whitespace and include order only.
fmt:
	$(ATLAS_SOURCES) | xargs -r clang-format -i

# Reports which files are not formatted correctly, without editing anything, and
# exits non-zero if any are. This is what CI runs from step 2.7. clang-format
# output varies between major versions, so CI pins the version installed here (18).
fmt-check:
	$(ATLAS_SOURCES) | xargs -r clang-format --dry-run --Werror

# Deletes every generated file. All build output lives under build/, so this is
# a complete reset; the next build reconfigures from scratch. trace.jsonl goes
# too, being the default --trace output of `make sim`.
clean:
	rm -rf build trace.jsonl

# Prints the target list.
help:
	@printf 'Atlas make targets. All of them forward to CMake.\n\n'
	@printf '  make            build the debug preset (the default)\n'
	@printf '  make release    build optimized\n'
	@printf '  make asan       build with ASan + UBSan\n'
	@printf '\n'
	@printf '  make sim        build debug, then run one simulation and report\n'
	@printf '                  overridable: make sim NODES=16 RATE=0.035\n'
	@printf '\n'
	@printf '  make test       run the test suite through ctest\n'
	@printf '  make test-asan  the same suite under ASan + UBSan\n'
	@printf '  make determinism  golden trace identical across all three presets\n'
	@printf '  make golden-update  regenerate the golden trace (read the diff first)\n'
	@printf '  make fmt        reformat all sources with clang-format\n'
	@printf '  make fmt-check  report unformatted files without editing\n'
	@printf '  make clean      delete build/ and trace.jsonl\n'
	@printf '\n'
	@printf 'The underlying commands, if you prefer them directly:\n'
	@printf '  cmake --preset debug && cmake --build --preset debug\n'

.PHONY: debug release asan sim test test-asan determinism golden-update fmt fmt-check clean help
