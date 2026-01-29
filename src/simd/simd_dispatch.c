/**
 * @file simd_dispatch.c
 * @brief Runtime SIMD capability detection and dispatch
 */

#include "simd_dispatch.h"
#include <string.h>

/* Global dispatch table */
fsd_simd_dispatch_t g_fsd_simd = {0};

/* CPU feature detection */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define FSD_X86 1

#if defined(_MSC_VER)
#include <intrin.h>
static void cpuid(int info[4], int leaf) {
    __cpuid(info, leaf);
}
static void cpuidex(int info[4], int leaf, int subleaf) {
    __cpuidex(info, leaf, subleaf);
}
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
static void cpuid(int info[4], int leaf) {
    __cpuid(leaf, info[0], info[1], info[2], info[3]);
}
static void cpuidex(int info[4], int leaf, int subleaf) {
    __cpuid_count(leaf, subleaf, info[0], info[1], info[2], info[3]);
}
#endif

static fsd_simd_caps_t detect_x86_caps(void) {
    fsd_simd_caps_t caps = FSD_SIMD_NONE;
    int info[4];

    /* Get basic features */
    cpuid(info, 1);

    /* SSE2: EDX bit 26 */
    if (info[3] & (1 << 26)) {
        caps |= FSD_SIMD_SSE2;
    }

    /* AVX: ECX bit 28 */
    if (info[2] & (1 << 28)) {
        caps |= FSD_SIMD_AVX;
    }

    /* Check for AVX2 */
    cpuid(info, 0);
    if (info[0] >= 7) {
        cpuidex(info, 7, 0);
        /* AVX2: EBX bit 5 */
        if (info[1] & (1 << 5)) {
            caps |= FSD_SIMD_AVX2;
        }
    }

    return caps;
}

#elif defined(__aarch64__) || defined(_M_ARM64)
#define FSD_ARM64 1

static fsd_simd_caps_t detect_arm_caps(void) {
    /* NEON is mandatory on AArch64 */
    return FSD_SIMD_NEON;
}

#elif defined(__arm__) || defined(_M_ARM)
#define FSD_ARM32 1

#if defined(__linux__)
#include <sys/auxv.h>
#include <asm/hwcap.h>
static fsd_simd_caps_t detect_arm_caps(void) {
    unsigned long hwcap = getauxval(AT_HWCAP);
    if (hwcap & HWCAP_NEON) {
        return FSD_SIMD_NEON;
    }
    return FSD_SIMD_NONE;
}
#else
static fsd_simd_caps_t detect_arm_caps(void) {
    /* Assume NEON on modern ARM */
    return FSD_SIMD_NEON;
}
#endif

#else
/* Unknown architecture */
static fsd_simd_caps_t detect_caps(void) {
    return FSD_SIMD_NONE;
}
#endif

static void setup_scalar_dispatch(void) {
    g_fsd_simd.caps = FSD_SIMD_NONE;
    g_fsd_simd.name = "scalar";
    g_fsd_simd.is_zero = fsd_scalar_is_zero;
    g_fsd_simd.is_one = fsd_scalar_is_one;
    g_fsd_simd.count_matches = fsd_scalar_count_matches;
}

#ifdef FSDIFF_HAS_AVX2
static void setup_avx2_dispatch(void) {
    g_fsd_simd.caps = FSD_SIMD_AVX2;
    g_fsd_simd.name = "AVX2";
    g_fsd_simd.is_zero = fsd_avx2_is_zero;
    g_fsd_simd.is_one = fsd_avx2_is_one;
    g_fsd_simd.count_matches = fsd_avx2_count_matches;
}
#endif

#ifdef FSDIFF_HAS_NEON
static void setup_neon_dispatch(void) {
    g_fsd_simd.caps = FSD_SIMD_NEON;
    g_fsd_simd.name = "NEON";
    g_fsd_simd.is_zero = fsd_neon_is_zero;
    g_fsd_simd.is_one = fsd_neon_is_one;
    g_fsd_simd.count_matches = fsd_neon_count_matches;
}
#endif

void fsd_simd_init(void) {
    /* Start with scalar as default */
    setup_scalar_dispatch();

#if defined(FSD_X86)
    fsd_simd_caps_t caps = detect_x86_caps();

#ifdef FSDIFF_HAS_AVX2
    if (caps & FSD_SIMD_AVX2) {
        setup_avx2_dispatch();
        return;
    }
#endif
    (void)caps;

#elif defined(FSD_ARM64) || defined(FSD_ARM32)
    fsd_simd_caps_t caps = detect_arm_caps();

#ifdef FSDIFF_HAS_NEON
    if (caps & FSD_SIMD_NEON) {
        setup_neon_dispatch();
        return;
    }
#endif
    (void)caps;
#endif
}

void fsd_simd_force_scalar(void) {
    setup_scalar_dispatch();
}

fsd_simd_caps_t fsd_simd_get_caps(void) {
    return g_fsd_simd.caps;
}

const char *fsd_simd_get_name(void) {
    return g_fsd_simd.name ? g_fsd_simd.name : "uninitialized";
}
