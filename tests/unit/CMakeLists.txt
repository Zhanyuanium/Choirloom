# ============================================================================
# tests/unit/CMakeLists.txt - M0-001 RationalTime slice unit tests (CTest).
#
# Part of the M0-002 rational-time harness: the `rational_time_tests` target is
# registered here and run by `dev.ps1 test|verify rational-time`. Build
# artifacts are disposable and live under build/dev/rational-time. This is a
# scoped harness target, not a full product test pipeline.
# ============================================================================

add_executable(choirloom_rational_time_tests
    rational_time_tests.cpp
)

target_link_libraries(choirloom_rational_time_tests PRIVATE choirloom_score)

target_compile_features(choirloom_rational_time_tests PRIVATE cxx_std_20)

# Absolute repository root so the test binary can locate the golden fixture and
# schema files from any working directory (CTest runs tests from the build
# tree, not the source tree).
get_filename_component(CHOIRLOOM_TEST_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_compile_definitions(choirloom_rational_time_tests PRIVATE
    CHOIRLOOM_TEST_SOURCE_DIR="${CHOIRLOOM_TEST_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(choirloom_rational_time_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(choirloom_rational_time_tests PRIVATE -Wall -Wextra -pedantic)
endif()

add_test(NAME rational_time_tests COMMAND choirloom_rational_time_tests)
