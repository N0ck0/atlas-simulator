cmake_minimum_required(VERSION 3.25)
project(atlas LANGUAGES CXX VERSION 0.1.0)

# ccache is a build-time convenience, never a requirement, so it is wired in
# only when present rather than declared as a dependency.
find_program(ATLAS_CCACHE ccache)
if(ATLAS_CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${ATLAS_CCACHE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -std=c++20 rather than -std=gnu++20. A GNU extension that compiles here but
# not under clang is a portability bug found in CI instead of at the keyboard.
set(CMAKE_CXX_EXTENSIONS OFF)

# Lets clangd and the VS Code C/C++ extension read the real flags instead of
# guessing them.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ATLAS_WERROR "Treat compiler warnings as errors" OFF)

# The warning set lives on one INTERFACE target so that libatlas, the CLI, and
# the test binaries cannot drift apart on it as S2 splits them up. Off by
# default and on in CI: a warning should stop a merge, not a local experiment.
# Flags are GCC/Clang spellings; those are the only two compilers targeted.
add_library(atlas_warnings INTERFACE)
target_compile_options(atlas_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion
  $<$<BOOL:${ATLAS_WERROR}>:-Werror>)

# The simulator core, built once and linked by both the CLI and (from step 2.4)
# the test binaries. A static library rather than an object library so that
# pybind11 can link it as-is in S6.
add_library(atlas_lib STATIC
  src/atlas/engine/simulator.cpp
  src/atlas/io/options.cpp
  src/atlas/io/report.cpp
  src/atlas/io/trace.cpp
  src/atlas/metrics/offered_load.cpp
  src/atlas/metrics/percentiles.cpp
  src/atlas/metrics/time_sample.cpp
  src/atlas/metrics/utilization.cpp)

# PUBLIC so anything linking atlas_lib can find its headers too. Includes are
# written from src/ down -- `#include "atlas/engine/clock.hpp"` -- so a header's
# path names its module and cannot collide with a system header.
target_include_directories(atlas_lib PUBLIC src)
target_link_libraries(atlas_lib PRIVATE atlas_warnings)
set_target_properties(atlas_lib PROPERTIES OUTPUT_NAME atlas)

# The CLI. Thin by design: everything it does lives in the library, so main()
# only wires a cluster, a workload and a trace writer together and runs them.
add_executable(atlas src/main.cpp)
target_link_libraries(atlas PRIVATE atlas_lib atlas_warnings)

option(ATLAS_BUILD_TESTS "Build the test suite" ON)

if(ATLAS_BUILD_TESTS)
  include(FetchContent)

  # Downloaded at configure time and built as part of this build, so a fresh
  # clone needs only cmake -- no install step, no submodule. Pinned to a release
  # tag rather than a branch: a dependency that moves on its own would make the
  # determinism guarantee a statement about today's checkout only.
  # v1.15.2 is b514bdc898e2951020cbdca1304b75f5950d1f59.
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)

  # GoogleMock is a separate mock-object library in the same repository and is
  # not used here; skipping it roughly halves the dependency build.
  set(BUILD_GMOCK OFF)
  set(INSTALL_GTEST OFF)

  FetchContent_MakeAvailable(googletest)

  # Must be called from the top-level list file, not from tests/, or ctest
  # writes its test list somewhere ctest --preset will not look for it.
  enable_testing()
  add_subdirectory(tests)
endif()
