###############################################################################
# FsdiffCompilerConfig.cmake - Compiler-specific configuration
###############################################################################

# Detect compiler
set(FSDIFF_COMPILER_GCC OFF)
set(FSDIFF_COMPILER_CLANG OFF)
set(FSDIFF_COMPILER_ICC OFF)
set(FSDIFF_COMPILER_MSVC OFF)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(FSDIFF_COMPILER_GCC ON)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(FSDIFF_COMPILER_CLANG ON)
elseif(CMAKE_C_COMPILER_ID STREQUAL "Intel")
    set(FSDIFF_COMPILER_ICC ON)
elseif(MSVC)
    set(FSDIFF_COMPILER_MSVC ON)
endif()

# Base compiler flags
set(FSDIFF_C_FLAGS_COMMON "")
set(FSDIFF_C_FLAGS_DEBUG "")
set(FSDIFF_C_FLAGS_RELEASE "")

###############################################################################
# GCC Configuration
###############################################################################
if(FSDIFF_COMPILER_GCC)
    list(APPEND FSDIFF_C_FLAGS_COMMON
        -Wall
        -Wextra
        -Wpedantic
        -Wformat=2
        -Wno-unused-parameter
        -ffunction-sections
        -fdata-sections
    )

    list(APPEND FSDIFF_C_FLAGS_DEBUG
        -g3
        -O0
        -fno-omit-frame-pointer
    )

    list(APPEND FSDIFF_C_FLAGS_RELEASE
        -O3
        -DNDEBUG
    )

    # Link-time optimization for release builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        include(CheckIPOSupported)
        check_ipo_supported(RESULT FSDIFF_LTO_SUPPORTED OUTPUT error)
        if(FSDIFF_LTO_SUPPORTED)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        endif()
    endif()

###############################################################################
# Clang Configuration
###############################################################################
elseif(FSDIFF_COMPILER_CLANG)
    list(APPEND FSDIFF_C_FLAGS_COMMON
        -Wall
        -Wextra
        -Wpedantic
        -Wformat=2
        -Wno-unused-parameter
        -ffunction-sections
        -fdata-sections
    )

    list(APPEND FSDIFF_C_FLAGS_DEBUG
        -g
        -O0
        -fno-omit-frame-pointer
    )

    list(APPEND FSDIFF_C_FLAGS_RELEASE
        -O3
        -DNDEBUG
    )

    # Sanitizers in debug mode (optional)
    option(FSDIFF_ENABLE_ASAN "Enable AddressSanitizer" OFF)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND FSDIFF_ENABLE_ASAN)
        list(APPEND FSDIFF_C_FLAGS_DEBUG -fsanitize=address,undefined)
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG
            "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
    endif()

###############################################################################
# Intel ICC Configuration (x64 Linux only)
###############################################################################
elseif(FSDIFF_COMPILER_ICC)
    # ICC is only supported on x64
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        message(FATAL_ERROR "Intel ICC is only supported on x64 architecture")
    endif()

    list(APPEND FSDIFF_C_FLAGS_COMMON
        -Wall
        -Wextra
        -diag-disable=10441   # Deprecated classic compiler warning
    )

    list(APPEND FSDIFF_C_FLAGS_DEBUG
        -g
        -O0
        -debug all
    )

    list(APPEND FSDIFF_C_FLAGS_RELEASE
        -O3
        -DNDEBUG
        -ipo                   # Interprocedural optimization
    )

    # ICC vectorization reports (optional)
    option(FSDIFF_ICC_VEC_REPORT "Enable ICC vectorization reports" OFF)
    if(FSDIFF_ICC_VEC_REPORT)
        list(APPEND FSDIFF_C_FLAGS_RELEASE -qopt-report=5 -qopt-report-phase=vec)
    endif()

###############################################################################
# MSVC Configuration
###############################################################################
elseif(FSDIFF_COMPILER_MSVC)
    list(APPEND FSDIFF_C_FLAGS_COMMON
        /W4
        /wd4100    # Unreferenced formal parameter
    )

    list(APPEND FSDIFF_C_FLAGS_DEBUG
        /Zi
        /Od
    )

    list(APPEND FSDIFF_C_FLAGS_RELEASE
        /O2
        /DNDEBUG
    )
endif()

###############################################################################
# Create interface library for compiler flags
###############################################################################

add_library(fsdiff_compiler_flags INTERFACE)

target_compile_options(fsdiff_compiler_flags INTERFACE
    ${FSDIFF_C_FLAGS_COMMON}
    $<$<CONFIG:Debug>:${FSDIFF_C_FLAGS_DEBUG}>
    $<$<CONFIG:Release>:${FSDIFF_C_FLAGS_RELEASE}>
    $<$<CONFIG:RelWithDebInfo>:${FSDIFF_C_FLAGS_RELEASE}>
)

# Platform-specific linker flags.
# For Release / MinSizeRel: strip local symbols at link time (-Wl,-s) so
# distributed binaries don't ship with debug names. RelWithDebInfo is
# explicitly excluded — that build type exists to keep debug info.
# MSVC writes debug info to a separate .pdb, so the .exe is already lean.
if(NOT WIN32)
    target_link_options(fsdiff_compiler_flags INTERFACE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Wl,--gc-sections>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Wl,-s>
    )
endif()
