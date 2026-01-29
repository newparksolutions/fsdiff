###############################################################################
# fsdiff library source configuration
###############################################################################

# Core sources (always compiled)
set(FSDIFF_CORE_SOURCES
    core/memory_pool.c
    core/crc32.c
    core/hash_table.c
    core/block_tracker.c
    io/mmap_reader.c
    io/buffered_writer.c
    stages/stage_identity.c
    stages/stage_relocation.c
    stages/stage_partial.c
    stages/stage_controller.c
    encoding/bkdf_header.c
    encoding/operation_encoder.c
    encoding/stream_writer.c
    simd/simd_dispatch.c
    simd/projection_scalar.c
    diff.c
    patch.c
)

# Object library for shared code
add_library(fsdiff_objects OBJECT ${FSDIFF_CORE_SOURCES})

target_include_directories(fsdiff_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fsdiff_objects
    PRIVATE
        fsdiff_compiler_flags
)

set_property(TARGET fsdiff_objects PROPERTY POSITION_INDEPENDENT_CODE ON)

###############################################################################
# SIMD Implementations (conditionally compiled)
###############################################################################

# AVX2 implementation
if(FSDIFF_HAS_AVX2)
    add_library(fsdiff_avx2 OBJECT
        simd/projection_avx2.c
    )

    target_include_directories(fsdiff_avx2
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(fsdiff_avx2 PRIVATE ${FSDIFF_AVX2_FLAGS})
    target_compile_definitions(fsdiff_avx2 PRIVATE FSDIFF_HAS_AVX2=1)
    target_link_libraries(fsdiff_avx2 PRIVATE fsdiff_compiler_flags)
    set_property(TARGET fsdiff_avx2 PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_compile_definitions(fsdiff_objects PRIVATE FSDIFF_HAS_AVX2=1)
endif()

# NEON implementation
if(FSDIFF_HAS_NEON)
    add_library(fsdiff_neon OBJECT
        simd/projection_neon.c
    )

    target_include_directories(fsdiff_neon
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(FSDIFF_NEON_FLAGS)
        target_compile_options(fsdiff_neon PRIVATE ${FSDIFF_NEON_FLAGS})
    endif()
    target_compile_definitions(fsdiff_neon PRIVATE FSDIFF_HAS_NEON=1)
    target_link_libraries(fsdiff_neon PRIVATE fsdiff_compiler_flags)
    set_property(TARGET fsdiff_neon PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_compile_definitions(fsdiff_objects PRIVATE FSDIFF_HAS_NEON=1)
endif()

###############################################################################
# Collect all object files
###############################################################################
set(FSDIFF_ALL_OBJECTS $<TARGET_OBJECTS:fsdiff_objects>)

if(FSDIFF_HAS_AVX2)
    list(APPEND FSDIFF_ALL_OBJECTS $<TARGET_OBJECTS:fsdiff_avx2>)
endif()

if(FSDIFF_HAS_NEON)
    list(APPEND FSDIFF_ALL_OBJECTS $<TARGET_OBJECTS:fsdiff_neon>)
endif()

###############################################################################
# Static Library
###############################################################################
if(FSDIFF_BUILD_STATIC)
    add_library(fsdiff_static STATIC ${FSDIFF_ALL_OBJECTS})

    set_target_properties(fsdiff_static PROPERTIES
        OUTPUT_NAME fsdiff
        ARCHIVE_OUTPUT_NAME fsdiff
    )

    target_include_directories(fsdiff_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    target_link_libraries(fsdiff_static
        PUBLIC
            ${MATH_LIBRARY}
    )

    install(TARGETS fsdiff_static
        EXPORT fsdiff-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT development
    )
endif()

###############################################################################
# Shared Library
###############################################################################
if(FSDIFF_BUILD_SHARED)
    add_library(fsdiff_shared SHARED ${FSDIFF_ALL_OBJECTS})

    set_target_properties(fsdiff_shared PROPERTIES
        OUTPUT_NAME fsdiff
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
    )

    # Generate export header
    generate_export_header(fsdiff_shared
        BASE_NAME FSDIFF
        EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/include/fsdiff/export.h"
    )

    target_include_directories(fsdiff_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    target_link_libraries(fsdiff_shared
        PUBLIC
            ${MATH_LIBRARY}
    )

    install(TARGETS fsdiff_shared
        EXPORT fsdiff-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT runtime
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT runtime
    )
endif()

###############################################################################
# Export targets
###############################################################################
install(EXPORT fsdiff-targets
    FILE FsdiffTargets.cmake
    NAMESPACE Fsdiff::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fsdiff
)
