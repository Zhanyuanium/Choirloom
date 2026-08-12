# ============================================================================
# Minimal root CMake configuration for the M0-001 RationalTime slice.
#
# Purpose: LOCAL COMPILER DIAGNOSTIC only. The project control plane is
# dev.ps1 (build/test/verify are NotImplemented pre-M0); CMake is NOT the
# project build system and must not be treated as project validation.
#
# Usage:
#   cmake -S . -B build/slice -G Ninja
#   cmake --build build/slice
#   ctest --test-dir build/slice --output-on-failure
# ============================================================================

cmake_minimum_required(VERSION 3.16)
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
