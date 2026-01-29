###############################################################################
# fsdiff tests
###############################################################################

enable_testing()

# Helper macro for test executables
macro(fsdiff_add_test name)
    add_executable(${name} ${name}.c)

    # Link against static library for testing
    if(FSDIFF_BUILD_STATIC)
        target_link_libraries(${name} PRIVATE fsdiff_static)
    else()
        target_link_libraries(${name} PRIVATE fsdiff_shared)
    endif()

    target_link_libraries(${name} PRIVATE fsdiff_compiler_flags)

    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Core module tests
fsdiff_add_test(test_crc32)
fsdiff_add_test(test_memory_pool)
fsdiff_add_test(test_block_tracker)

# I/O tests
fsdiff_add_test(test_mmap_reader)
fsdiff_add_test(test_buffered_writer)

# Encoding tests
fsdiff_add_test(test_bkdf_header)
fsdiff_add_test(test_decoder)

# Integration tests
fsdiff_add_test(test_roundtrip)
