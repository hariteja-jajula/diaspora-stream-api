# (C) 2026 The University of Chicago
# See COPYRIGHT in top-level directory.
#
# C bindings library: diaspora-c
#
# Provides a driver-agnostic C facade over the Diaspora Stream API
# (producer side). Built into a separate shared library that depends on
# the C++ diaspora-stream-api library; consumers link `-ldiaspora-c`
# (and pkg-config pulls in `-ldiaspora-stream-api` via Requires).
#
# Header lives at include/diaspora/diaspora_c.h (installed alongside
# the C++ headers).
#
# Conventions inherited from src/CMakeLists.txt: target name parallels
# the library name (`diaspora-c`); version+soversion mirror upstream;
# export-set shared with `diaspora-stream-api-targets` so
# `find_package(diaspora-stream-api)` resolves either; pkg-config file
# generated from .pc.in.

set (diaspora-c-src
     diaspora_c.cpp)

add_library (diaspora-c ${diaspora-c-src})

# Link against the C++ API library (PUBLIC: consumers of diaspora-c
# transitively get the C++ symbols too, which is correct since the
# C bindings wrap C++ value types whose layout must match).
target_link_libraries (diaspora-c
    PUBLIC
    diaspora-stream-api
    PRIVATE
    coverage_config
    warnings_config)

target_include_directories (diaspora-c PUBLIC $<INSTALL_INTERFACE:include>)
target_include_directories (diaspora-c BEFORE PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>)

set_target_properties (diaspora-c
    PROPERTIES VERSION ${DIASPORA_STREAM_API_VERSION}
    SOVERSION ${DIASPORA_STREAM_API_VERSION_MAJOR})

# pkg-config file for downstream pure-C consumers (Darshan, etc.).
# Set DEST_DIR and DIASPORA_API_VERSION locally -- CMake parent-scope
# variables don't auto-propagate into add_subdirectory'd scopes for
# configure_file substitution. Mirror what src/CMakeLists.txt does.
set (DEST_DIR "${CMAKE_INSTALL_PREFIX}")
set (DIASPORA_API_VERSION "${DIASPORA_STREAM_API_VERSION}")
configure_file ("diaspora-c.pc.in" "diaspora-c.pc" @ONLY)

# Install: library, header (already covered by parent's
# install(DIRECTORY ../include/diaspora DESTINATION include) at
# src/CMakeLists.txt), pkg-config.
install (TARGETS diaspora-c
         EXPORT diaspora-stream-api-targets
         ARCHIVE DESTINATION lib
         LIBRARY DESTINATION lib
         RUNTIME DESTINATION bin
         INCLUDES DESTINATION include)
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/diaspora-c.pc"
         DESTINATION "lib/pkgconfig/")
