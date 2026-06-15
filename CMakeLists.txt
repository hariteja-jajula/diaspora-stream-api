# (C) 2020 The University of Chicago
# See COPYRIGHT in top-level directory.
cmake_minimum_required (VERSION 3.21)
project (diaspora-stream-api C CXX)
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing ()

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy (SET CMP0135 NEW)
endif ()

# library version set here (e.g. for shared libs).
set (DIASPORA_STREAM_API_VERSION_MAJOR 0)
set (DIASPORA_STREAM_API_VERSION_MINOR 5)
set (DIASPORA_STREAM_API_VERSION_PATCH 7)
set (DIASPORA_STREAM_API_VERSION
    "${DIASPORA_STREAM_API_VERSION_MAJOR}.${DIASPORA_STREAM_API_VERSION_MINOR}.${DIASPORA_STREAM_API_VERSION_PATCH}")

if (DEFINED SKBUILD_PROJECT_VERSION AND NOT "${SKBUILD_PROJECT_VERSION}" STREQUAL "${DIASPORA_STREAM_API_VERSION}")
    message (FATAL_ERROR
"Version in pyproject.toml (${SKBUILD_PROJECT_VERSION}) \
does not match version in CMakeLists.txt (${DIASPORA_STREAM_API_VERSION})")
endif ()

add_library (coverage_config INTERFACE)
add_library (warnings_config INTERFACE)

#set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
#set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

option (ENABLE_TESTS     "Build tests" OFF)
option (ENABLE_COVERAGE  "Build with coverage" OFF)
option (ENABLE_PYTHON    "Build the Python module" OFF)
option (ENABLE_BENCHMARKS "Build benchmarks" OFF)
option (ENABLE_C_API     "Build the pure-C bindings (diaspora-c)" OFF)

if (ENABLE_TESTS)
    set (ENABLE_BENCHMARKS ON)
endif ()

# add our cmake module directory to the path
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
     "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# link shared lib with full rpath
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if (NOT DEFINED SKBUILD_PROJECT_VERSION)
    list (APPEND CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
else ()
    list (APPEND CMAKE_INSTALL_RPATH "\$ORIGIN/lib")
    list (APPEND CMAKE_INSTALL_RPATH "\$ORIGIN")
endif ()

# setup cache variables for ccmake
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release
         CACHE STRING "Choose the type of build." FORCE)
    set_property (CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                  "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif ()
set (CMAKE_PREFIX_PATH "" CACHE STRING "External dependencies path")
set (BUILD_SHARED_LIBS "ON" CACHE BOOL "Build a shared library")

if (ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options (coverage_config INTERFACE
        -O0        # no optimization
        -g         # generate debug info
        --coverage # sets all required flags
    )
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.13)
        target_link_options (coverage_config INTERFACE --coverage)
    else ()
        target_link_libraries (coverage_config INTERFACE --coverage)
    endif ()
endif ()

if (CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo|Debug")
    target_compile_options (warnings_config INTERFACE
        -Wextra -Wall -Wpedantic
    )
endif ()

# search for nlohmann/json and schema validator
if (NOT DEFINED SKBUILD_PROJECT_NAME)
    find_package (nlohmann_json)
    find_package (nlohmann_json_schema_validator REQUIRED)
    find_package (TCLAP REQUIRED)
    find_package (spdlog REQUIRED)
    find_package (yaml-cpp REQUIRED)
else ()
    # Download nlohmann_json and nlohmann_json_schema_validator from github
    include (FetchContent)
    FetchContent_Declare (
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable (nlohmann_json)
    FetchContent_Declare (
        nlohmann_json_schema_validator
        URL https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.3.0.tar.gz
    )
    FetchContent_MakeAvailable (nlohmann_json_schema_validator)
    install (DIRECTORY ${nlohmann_json_SOURCE_DIR}/include/nlohmann
             DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install (TARGETS nlohmann_json nlohmann_json_schema_validator
             EXPORT diaspora-stream-api-targets
             ARCHIVE DESTINATION lib
             LIBRARY DESTINATION lib
             RUNTIME DESTINATION bin
             INCLUDES DESTINATION include)
     # Download TCLAP from github
    FetchContent_Declare (
        tclap
        GIT_REPOSITORY https://github.com/mirror/tclap.git
        GIT_TAG        v1.2.5
    )
    FetchContent_MakeAvailable (tclap)
    # TCLAP is header-only, set the include path manually
    set (TCLAP_INCLUDE_PATH "${tclap_SOURCE_DIR}/include")
    # Download spdlog from github
    FetchContent_Declare (
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.14.1
    )
    FetchContent_MakeAvailable (spdlog)
    # Download yaml-cpp from github
    FetchContent_Declare (
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
    )
    set (YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable (yaml-cpp)
endif ()

if (ENABLE_PYTHON)
    find_package (Python3 COMPONENTS Interpreter Development REQUIRED)
    find_package (pybind11 REQUIRED)
    add_subdirectory (python)
endif ()

add_subdirectory (src)
if (ENABLE_C_API)
    add_subdirectory (src/c)
endif ()
add_subdirectory (bin)
add_subdirectory (docs/_code)

if (ENABLE_TESTS)
    enable_testing ()
    find_package (Catch2 QUIET)
    if (NOT Catch2_FOUND)
        include (FetchContent)
        FetchContent_Declare (
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.6.0
        )
        FetchContent_MakeAvailable (Catch2)
    endif ()
    add_subdirectory (tests)
endif ()

if (ENABLE_BENCHMARKS)
    find_package (MPI REQUIRED)
    add_subdirectory (benchmarks)
endif ()
