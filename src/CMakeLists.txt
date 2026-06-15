
set (src-files
     Factory.cpp
     TopicHandle.cpp
     Producer.cpp
     Consumer.cpp
     DataDescriptor.cpp
     Serializer.cpp
     PartitionSelector.cpp
     Validator.cpp
     EventbridgeValidator.cpp)

file (GLOB files-driver-src ${CMAKE_CURRENT_SOURCE_DIR}/files-driver/*.cpp)

# load package helper for generating cmake CONFIG packages
include (CMakePackageConfigHelpers)

# where to install files for "find_package"
set (diaspora-stream-api-pkg "share/cmake/diaspora-stream-api")

# set version fo shared library
set (diaspora-stream-api-vers "${DIASPORA_STREAM_API_VERSION_MAJOR}.${DIASPORA_STREAM_API_VERSION_MINOR}")

# library
add_library (diaspora-stream-api ${src-files} ${files-driver-src})
target_link_libraries (diaspora-stream-api
    PUBLIC
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    ${CMAKE_DL_LIBS}
    PRIVATE
    spdlog::spdlog_header_only
    coverage_config
    warnings_config)
target_include_directories (diaspora-stream-api PUBLIC $<INSTALL_INTERFACE:include>)
target_include_directories (diaspora-stream-api BEFORE PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)
target_include_directories (diaspora-stream-api BEFORE PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
set_target_properties (diaspora-stream-api
    PROPERTIES VERSION ${DIASPORA_STREAM_API_VERSION}
    SOVERSION ${DIASPORA_STREAM_API_VERSION_MAJOR})


# installation stuff (packaging and install commands)
write_basic_package_version_file (
    "diaspora-stream-api-config-version.cmake"
    VERSION ${DIASPORA_STREAM_API_VERSION}
    COMPATIBILITY AnyNewerVersion)

# generate our config file for find_package()
set (INCLUDE_INSTALL_DIR include/)
set (LIB_INSTALL_DIR lib/)
configure_package_config_file (diaspora-stream-api-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/diaspora-stream-api-config.cmake
  INSTALL_DESTINATION ${LIB_INSTALL_DIR}/diaspora-stream-api/cmake
  PATH_VARS INCLUDE_INSTALL_DIR)

# some bits for the pkg-config file
set (DEST_DIR "${CMAKE_INSTALL_PREFIX}")
set (DIASPORA_API_PRIVATE_LIBS "-ldiaspora-stream-api")
configure_file ("diaspora-stream-api.pc.in" "diaspora-stream-api.pc" @ONLY)

# configure config.h
configure_file ("Config.h.in" "Config.h" @ONLY)

install (TARGETS diaspora-stream-api ${NLOHMANN_JSON_TARGETS}
         EXPORT diaspora-stream-api-targets
         ARCHIVE DESTINATION lib
         LIBRARY DESTINATION lib
         RUNTIME DESTINATION bin
         INCLUDES DESTINATION include)
install (EXPORT diaspora-stream-api-targets
         DESTINATION ${diaspora-stream-api-pkg}
         FILE "diaspora-stream-api-targets.cmake")
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/diaspora-stream-api-config.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/diaspora-stream-api-config-version.cmake"
         DESTINATION ${diaspora-stream-api-pkg} )
install (DIRECTORY ../include/diaspora
         DESTINATION include
         FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/diaspora-stream-api.pc"
         DESTINATION "lib/pkgconfig/")
