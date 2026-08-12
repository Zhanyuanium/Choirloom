# ============================================================================
# core/score/CMakeLists.txt - M0-001/M0-003 portable-core slice targets.
#
# Shared choirloom_score library for the M0-001 RationalTime and M0-003
# EntityId/Revision primitives. Built by the control plane via
#   dev.ps1 build|test|verify rational-time
#   dev.ps1 build|test|verify entity-revision
# which write disposable build artifacts under build/dev/<scope>.
# This is NOT a full product build pipeline, and no CI gate is implied.
# ============================================================================

add_library(choirloom_score STATIC
    rational_time.cpp
    entity_revision.cpp
    schema_foundation.cpp
)

target_include_directories(choirloom_score PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(choirloom_score PUBLIC cxx_std_20)
