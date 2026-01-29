###############################################################################
# fsdiff - Filesystem Binary Diff/Patch Library
###############################################################################

cmake_minimum_required(VERSION 3.16)

project(fsdiff
    VERSION 1.0.0
    DESCRIPTION "Block-level binary diff/patch library for filesystem images"
    LANGUAGES C
)

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build type default
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

###############################################################################
# Options
###############################################################################

option(FSDIFF_BUILD_SHARED  "Build shared library" ON)
option(FSDIFF_BUILD_STATIC  "Build static library" ON)
option(FSDIFF_BUILD_CLI     "Build command-line tool" ON)
option(FSDIFF_BUILD_TESTS   "Build test suite" ON)
option(FSDIFF_ENABLE_SIMD   "Enable SIMD optimizations" ON)
option(FSDIFF_ENABLE_AVX2   "Enable AVX2 (x64 only)" ON)
option(FSDIFF_ENABLE_NEON   "Enable NEON (ARM only)" ON)

# Symbol visibility
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

###############################################################################
# Compiler Configuration
###############################################################################

include(FsdiffCompilerConfig)
include(FsdiffSIMD)

###############################################################################
# Dependencies
###############################################################################

# Math library
include(CheckLibraryExists)
check_library_exists(m sin "" HAVE_LIBM)
if(HAVE_LIBM)
    set(MATH_LIBRARY m)
endif()

###############################################################################
# Generate Headers
###############################################################################

# Export header for shared library symbols
include(GenerateExportHeader)

# Version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/fsdiff/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/fsdiff/version.h"
    @ONLY
)

###############################################################################
# Subdirectories
###############################################################################

add_subdirectory(src)

if(FSDIFF_BUILD_CLI)
    add_subdirectory(tools)
endif()

if(FSDIFF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

###############################################################################
# Installation
###############################################################################

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(FSDIFF_CMAKE_CONFIG_DEST "${CMAKE_INSTALL_LIBDIR}/cmake/fsdiff")

# Package config
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FsdiffConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FsdiffConfig.cmake"
    INSTALL_DESTINATION "${FSDIFF_CMAKE_CONFIG_DEST}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/FsdiffConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/FsdiffConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/FsdiffConfigVersion.cmake"
    DESTINATION "${FSDIFF_CMAKE_CONFIG_DEST}"
)

# Headers
install(
    DIRECTORY include/fsdiff
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h"
)

install(
    DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/fsdiff"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h"
)

# pkg-config
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fsdiff.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fsdiff.pc"
    @ONLY
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/fsdiff.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

###############################################################################
# Summary
###############################################################################

message(STATUS "")
message(STATUS "fsdiff ${PROJECT_VERSION} Configuration Summary")
message(STATUS "========================================")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler:        ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Install prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Shared library:  ${FSDIFF_BUILD_SHARED}")
message(STATUS "  Static library:  ${FSDIFF_BUILD_STATIC}")
message(STATUS "  CLI tool:        ${FSDIFF_BUILD_CLI}")
message(STATUS "  Tests:           ${FSDIFF_BUILD_TESTS}")
message(STATUS "")
message(STATUS "SIMD:")
message(STATUS "  AVX2:            ${FSDIFF_HAS_AVX2}")
message(STATUS "  NEON:            ${FSDIFF_HAS_NEON}")
message(STATUS "")
