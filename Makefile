# Temporary build for Stage 1; replaced by CMake in S2.

CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2

atlas: src/atlas.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: atlas
	./atlas

# One real simulation instead of the self-checks. Any parameter can be
# overridden on the command line:  make sim NODES=16 RATE=0.035
SEED  ?= 7
NODES ?= 8
JOBS  ?= 2000
RATE  ?= 0.025
TRACE ?= trace.jsonl

sim: atlas
	./atlas --seed $(SEED) --nodes $(NODES) --jobs $(JOBS) --rate $(RATE) \
	        --trace $(TRACE)

# Invariant 4: the same seed must produce a byte-identical trace regardless of
# optimization level. This needs two binaries at once, so it cannot be one of
# the self-checks inside atlas. Optimization flags are spelled out rather than
# taken from CXXFLAGS, since varying them is the entire point.
check-determinism: src/atlas.cpp
	$(CXX) -std=c++20 -O0 -o /tmp/atlas-O0 $<
	$(CXX) -std=c++20 -O2 -o /tmp/atlas-O2 $<
	/tmp/atlas-O0 --seed 7 --trace /tmp/atlas-trace-O0.jsonl > /dev/null
	/tmp/atlas-O2 --seed 7 --trace /tmp/atlas-trace-O2.jsonl > /dev/null
	cmp /tmp/atlas-trace-O0.jsonl /tmp/atlas-trace-O2.jsonl
	@echo "PASS  determinism: identical trace across -O0 and -O2"

clean:
	rm -f atlas trace.jsonl

.PHONY: run sim clean check-determinism
