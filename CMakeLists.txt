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

add_executable(atlas src/atlas.cpp)
target_link_libraries(atlas PRIVATE atlas_warnings)

# Invariant 4 needs two binaries built at different optimization levels, so it
# cannot be a normal test target. Temporary scaffolding: it becomes a ctest
# case against a checked-in golden trace in step 2.6.
add_custom_target(check-determinism
  COMMAND ${CMAKE_COMMAND}
          -D "CXX=${CMAKE_CXX_COMPILER}"
          -D "SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/atlas.cpp"
          -D "WORKDIR=${CMAKE_CURRENT_BINARY_DIR}/determinism"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckDeterminism.cmake"
  VERBATIM)
