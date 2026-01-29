/**
 * @file simd_dispatch.h
 * @brief Runtime SIMD dispatch interface
 */

#ifndef FSDIFF_SIMD_DISPATCH_H
#define FSDIFF_SIMD_DISPATCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SIMD capability flags
 */
typedef enum {
    FSD_SIMD_NONE   = 0,
    FSD_SIMD_SSE2   = (1 << 0),
    FSD_SIMD_AVX    = (1 << 1),
    FSD_SIMD_AVX2   = (1 << 2),
    FSD_SIMD_NEON   = (1 << 3),
} fsd_simd_caps_t;

/**
 * Function pointer types for SIMD operations
 */

/* Check if memory region is all zeros */
typedef bool (*fsd_is_zero_fn)(const void *data, size_t len);

/* Check if memory region is all 0xFF */
typedef bool (*fsd_is_one_fn)(const void *data, size_t len);

/* Count matching bytes between two buffers */
typedef size_t (*fsd_count_matches_fn)(const uint8_t *a,
                                       const uint8_t *b,
                                       size_t len);

/**
 * SIMD dispatch table
 */
typedef struct {
    fsd_simd_caps_t caps;
    const char *name;

    fsd_is_zero_fn is_zero;
    fsd_is_one_fn is_one;
    fsd_count_matches_fn count_matches;
} fsd_simd_dispatch_t;

/**
 * Global dispatch table (initialized by fsd_simd_init)
 */
extern fsd_simd_dispatch_t g_fsd_simd;

/**
 * Initialize SIMD dispatch based on CPU capabilities.
 * Called automatically by fsd_init().
 */
void fsd_simd_init(void);

/**
 * Force use of scalar implementations (for testing).
 */
void fsd_simd_force_scalar(void);

/**
 * Get current SIMD capabilities.
 */
fsd_simd_caps_t fsd_simd_get_caps(void);

/**
 * Get name of current SIMD implementation.
 */
const char *fsd_simd_get_name(void);

/* Scalar implementations (always available) */
bool fsd_scalar_is_zero(const void *data, size_t len);
bool fsd_scalar_is_one(const void *data, size_t len);
size_t fsd_scalar_count_matches(const uint8_t *a, const uint8_t *b, size_t len);

#ifdef FSDIFF_HAS_AVX2
/* AVX2 implementations */
bool fsd_avx2_is_zero(const void *data, size_t len);
bool fsd_avx2_is_one(const void *data, size_t len);
size_t fsd_avx2_count_matches(const uint8_t *a, const uint8_t *b, size_t len);
#endif

#ifdef FSDIFF_HAS_NEON
/* NEON implementations */
bool fsd_neon_is_zero(const void *data, size_t len);
bool fsd_neon_is_one(const void *data, size_t len);
size_t fsd_neon_count_matches(const uint8_t *a, const uint8_t *b, size_t len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_SIMD_DISPATCH_H */
