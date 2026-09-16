# Temporary build for Stage 1; replaced by CMake in S2.

CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -O2

atlas: src/atlas.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

run: atlas
	./atlas

clean:
	rm -f atlas

.PHONY: run clean
