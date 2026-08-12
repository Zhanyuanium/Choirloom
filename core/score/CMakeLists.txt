# ============================================================================
# core/score/CMakeLists.txt - M0-001 RationalTime slice build target.
#
# Part of the M0-002 harness: the only CMake target configured for the
# rational-time scope. It is built by the control plane via
#   dev.ps1 build|test|verify rational-time
# which writes disposable build artifacts under build/dev/rational-time.
# This is NOT a full product build pipeline, and no CI gate is implied.
# ============================================================================

add_library(choirloom_score STATIC
    rational_time.cpp
)

target_include_directories(choirloom_score PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(choirloom_score PUBLIC cxx_std_20)
