# ============================================================================
# core/score/CMakeLists.txt — M0-001 RationalTime slice build target.
#
# This is a NARROWLY SCOPED LOCAL BUILD SETUP for the first portable-core
# vertical slice. It is NOT the project build system: the repository has no
# product build/harness pipeline yet (dev.ps1 build/test/verify report
# NotImplemented until M0). This file exists so the slice can be compiled and
# tested with CMake/CTest as a LOCAL COMPILER DIAGNOSTIC only.
# ============================================================================

add_library(choirloom_score STATIC
    rational_time.cpp
)

target_include_directories(choirloom_score PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(choirloom_score PUBLIC cxx_std_20)
