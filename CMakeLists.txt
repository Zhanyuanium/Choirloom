# ============================================================================
# Root CMake configuration for the M0-001/M0-002 RationalTime slice.
#
# Purpose: the current HARNESS TOOLCHAIN for the rational-time scope. It is
# invoked by the project control plane via:
#   dev.ps1 build|test|verify rational-time
# (which calls cmake/ctest against the deterministic build directory
# build/dev/rational-time). This is NOT a full product build/test pipeline:
# it builds and tests only the RationalTime slice, and it is not a CI gate.
#
# Manual usage (diagnostic only; prefer dev.ps1):
#   cmake -S . -B build/dev/rational-time -G Ninja
#   cmake --build build/dev/rational-time --target choirloom_rational_time_tests
#   ctest --test-dir build/dev/rational-time --output-on-failure -R '^rational_time_tests$'
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(choirloom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

enable_testing()

add_subdirectory(core/score)
add_subdirectory(tests/unit)
