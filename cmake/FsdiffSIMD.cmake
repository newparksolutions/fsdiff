###############################################################################
# FsdiffSIMD.cmake - SIMD Detection and Configuration
###############################################################################

include(CheckCSourceCompiles)
include(CheckCCompilerFlag)

# Architecture detection
set(FSDIFF_ARCH_X64 OFF)
set(FSDIFF_ARCH_ARM OFF)
set(FSDIFF_ARCH_ARM64 OFF)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(FSDIFF_ARCH_X64 ON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(FSDIFF_ARCH_ARM64 ON)
    set(FSDIFF_ARCH_ARM ON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    set(FSDIFF_ARCH_ARM ON)
endif()

# Initialize SIMD detection results
set(FSDIFF_HAS_AVX2 OFF)
set(FSDIFF_HAS_NEON OFF)
set(FSDIFF_AVX2_FLAGS "")
set(FSDIFF_NEON_FLAGS "")

###############################################################################
# AVX2 Detection (x64 only)
###############################################################################
if(FSDIFF_ENABLE_SIMD AND FSDIFF_ENABLE_AVX2 AND FSDIFF_ARCH_X64)
    message(STATUS "Checking for AVX2 support...")

    # Determine AVX2 compiler flag
    if(FSDIFF_COMPILER_GCC OR FSDIFF_COMPILER_CLANG)
        set(AVX2_TEST_FLAG "-mavx2")
    elseif(FSDIFF_COMPILER_ICC)
        set(AVX2_TEST_FLAG "-xCORE-AVX2")
    elseif(FSDIFF_COMPILER_MSVC)
        set(AVX2_TEST_FLAG "/arch:AVX2")
    else()
        set(AVX2_TEST_FLAG "")
    endif()

    if(AVX2_TEST_FLAG)
        set(CMAKE_REQUIRED_FLAGS "${AVX2_TEST_FLAG}")

        check_c_source_compiles("
            #include <immintrin.h>
            int main() {
                __m256i a = _mm256_set1_epi32(1);
                __m256i b = _mm256_set1_epi32(2);
                __m256i c = _mm256_add_epi32(a, b);
                return _mm256_extract_epi32(c, 0);
            }
        " FSDIFF_HAS_AVX2_COMPILE)

        unset(CMAKE_REQUIRED_FLAGS)

        if(FSDIFF_HAS_AVX2_COMPILE)
            set(FSDIFF_HAS_AVX2 ON)
            set(FSDIFF_AVX2_FLAGS "${AVX2_TEST_FLAG}")
            message(STATUS "  AVX2 supported with flag: ${AVX2_TEST_FLAG}")
        else()
            message(STATUS "  AVX2 not supported by compiler")
        endif()
    endif()
endif()

###############################################################################
# NEON Detection (ARM only)
###############################################################################
if(FSDIFF_ENABLE_SIMD AND FSDIFF_ENABLE_NEON AND FSDIFF_ARCH_ARM)
    message(STATUS "Checking for NEON support...")

    # NEON is typically available by default on ARM64
    if(FSDIFF_ARCH_ARM64)
        set(NEON_TEST_FLAG "")
    else()
        # 32-bit ARM may need -mfpu=neon
        if(FSDIFF_COMPILER_GCC OR FSDIFF_COMPILER_CLANG)
            set(NEON_TEST_FLAG "-mfpu=neon")
        else()
            set(NEON_TEST_FLAG "")
        endif()
    endif()

    set(CMAKE_REQUIRED_FLAGS "${NEON_TEST_FLAG}")

    check_c_source_compiles("
        #include <arm_neon.h>
        int main() {
            int32x4_t a = vdupq_n_s32(1);
            int32x4_t b = vdupq_n_s32(2);
            int32x4_t c = vaddq_s32(a, b);
            return vgetq_lane_s32(c, 0);
        }
    " FSDIFF_HAS_NEON_COMPILE)

    unset(CMAKE_REQUIRED_FLAGS)

    if(FSDIFF_HAS_NEON_COMPILE)
        set(FSDIFF_HAS_NEON ON)
        set(FSDIFF_NEON_FLAGS "${NEON_TEST_FLAG}")
        if(NEON_TEST_FLAG)
            message(STATUS "  NEON supported with flag: ${NEON_TEST_FLAG}")
        else()
            message(STATUS "  NEON supported (built-in)")
        endif()
    else()
        message(STATUS "  NEON not supported by compiler")
    endif()
endif()

###############################################################################
# SIMD configuration header
###############################################################################
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/fsdiff/simd_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/fsdiff/simd_config.h"
    @ONLY
)
