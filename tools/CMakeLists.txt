###############################################################################
# fsdiff CLI tool
###############################################################################

add_executable(fsdiff_cli
    fsdiff.c
)

set_target_properties(fsdiff_cli PROPERTIES
    OUTPUT_NAME fsdiff
)

# Always link against static library for CLI
target_link_libraries(fsdiff_cli PRIVATE fsdiff_static)
target_link_libraries(fsdiff_cli PRIVATE fsdiff_compiler_flags)

install(TARGETS fsdiff_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT runtime
)
