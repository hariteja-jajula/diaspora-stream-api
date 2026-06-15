# (C) 2026 The University of Chicago
# See COPYRIGHT in top-level directory.
#
# C-bindings smoke test. Pure C; mirrors the conventions in the
# parent tests/CMakeLists.txt (add_executable + add_test + library
# search path env var + 120 s TIMEOUT).
#
# This test is configurable via env vars:
#   DIASPORA_C_TEST_DRIVER_NAME, DIASPORA_C_TEST_DRIVER_JSON,
#   DIASPORA_C_TEST_TOPIC_NAME
# If those are unset, the test exits 77 (SKIP) instead of failing,
# so that `ctest` runs by an unconfigured developer don't report a
# spurious failure.

add_executable (TestDiasporaC test_diaspora_c.c)
target_link_libraries (TestDiasporaC PRIVATE diaspora-c)

# Use the C standard explicitly so the test compiles even if a future
# diaspora_c.h pulls in C99/C11-only constructs deliberately.
set_target_properties (TestDiasporaC
    PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)

add_test (NAME TestDiasporaC COMMAND ./TestDiasporaC)
set_tests_properties (TestDiasporaC PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 120
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/src:${CMAKE_BINARY_DIR}/src/c:$ENV{LD_LIBRARY_PATH}")
