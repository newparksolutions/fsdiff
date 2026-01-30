# fsdiff - Binary Block-Level Diff Library

High-performance binary diff/patch library optimized for large filesystem images and disk images.

fsdiff gives good performance on large filesystem images which contain executable files with slight changes.  On executable files themselves (much less than 1 GB) bsdiff usually gives better compression.  On filesystem images which don't have much executable code, or have a lot of compressed executable code, rdiff usually gives similar compression in a shorter time.

## Features

- **Block-level matching** with identity, relocation, and partial matching stages
- **SIMD acceleration** (AVX2 on x86-64, NEON on ARM, scalar fallback)
- **Local search partial matching** with ±32KB offset range
- **Efficient encoding** with sparse/dense diff formats and base-128 varint compression
- **Cross-platform** (Linux, Windows, ARM)
- **Large file support** via memory-mapped I/O
- **Designed for external compression** (xz, lzma)

## Performance

|Test case|Original uncompressed|fsdiff uncompressed|fsdiff and xz -9|rdiff uncompressed|rdiff and xz -9|xdelta|
|---------|---------------------|-------------------|----------------|------------------|---------------|------|
|Raspberry Pi OS Full 2025-12-04 vs 2025-11-24|9208 MiB|737 MiB|**200 MiB**|1161 MiB|292 MiB|N/A|
|Debian Cloud Generic AMD64 20260129-2372 vs 20260112-2355|3072 MiB|78 MiB|**17 MiB**|210 MiB|43 MiB|388 MiB|

It is not possible to evaluate xdelta performance on some images because xdelta does not support files of 4 GiB or more.  bsdiff has not been included because, while it has excellent performance on single binaries, its memory requirements make it unsuitable for use on multi-gigabyte images.

 - rdiff version 2.3.4
 - xz version 5.6.1+really5.4.5-1ubuntu0.2
 - xdelta version 1.1.3

## Build Requirements

### All Platforms
- CMake 3.15 or later
- C99 compiler

### Linux
- GCC 7+ or Clang 8+
- Make

### Windows
- Visual Studio 2019 or later (MSVC)
- CMake

### ARM
- GCC with NEON support (AArch64 or ARMv7 with NEON)

## Building

### Linux / macOS

```bash
# Configure
cmake -B build -S .

# Build
cmake --build build --parallel

# Run tests
cd build && ctest
```

### Windows (MSVC)

```cmd
REM Configure
cmake -B build -S .

REM Build
cmake --build build --config Release --parallel

REM Run tests
cd build
ctest -C Release
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `FSDIFF_BUILD_SHARED` | ON | Build shared library |
| `FSDIFF_BUILD_STATIC` | ON | Build static library |
| `FSDIFF_BUILD_CLI` | ON | Build command-line tool |
| `FSDIFF_BUILD_TESTS` | ON | Build test suite |
| `FSDIFF_ENABLE_SIMD` | ON | Enable SIMD optimizations |
| `FSDIFF_ENABLE_AVX2` | ON | Enable AVX2 (x86-64 only) |
| `FSDIFF_ENABLE_NEON` | ON | Enable NEON (ARM only) |

Example: Build without SIMD for debugging
```bash
cmake -B build -S . -DFSDIFF_ENABLE_SIMD=OFF
```

## Usage

### Command-Line Tool

The `fsdiff` tool provides three commands:

#### Create a Patch (delta)

```bash
fsdiff delta [options] <source> <destination> <patch>
```

**Options:**
- `-b, --block-size <log2>` - Block size as power of 2 (default: 12 = 4096 bytes)
- `-r, --search-radius <n>` - Search radius for relocation (default: 8 blocks)
- `-t, --threshold <f>` - Partial match threshold 0.0-1.0 (default: 0.5)
- `-v, --verbose` - Show progress and statistics
- `-V, --very-verbose` - Detailed output from all matching stages
- `--scalar` - Force scalar (non-SIMD) implementation
- `--no-identity` - Disable identity matching
- `--no-relocation` - Disable relocation matching
- `--no-partial` - Disable partial matching

**Example:**
```bash
fsdiff delta -v disk_v1.img disk_v2.img update.bkdf
```

#### Apply a Patch (patch)

```bash
fsdiff patch [options] <source> <patch> <output>
```

**Options:**
- `-v, --verbose` - Show progress

**Example:**
```bash
fsdiff patch disk_v1.img update.bkdf disk_v2_rebuilt.img
```

#### Display Patch Info (info)

```bash
fsdiff info <patch>
```

**Example:**
```bash
fsdiff info update.bkdf
```

### Library API

#### Basic Usage

```c
#include <fsdiff/fsdiff.h>

/* Initialize library */
fsd_init();

/* Create a patch */
fsd_diff_ctx_t *diff_ctx = NULL;
fsd_diff_options_t opts;
fsd_diff_options_init(&opts);

fsd_diff_create(&diff_ctx, &opts);
fsd_diff_files(diff_ctx, "source.img", "dest.img", "patch.bkdf");

/* Get statistics */
fsd_diff_stats_t stats;
fsd_diff_get_stats(diff_ctx, &stats);
printf("Identity: %lu, Relocate: %lu, Partial: %lu\n",
       stats.identity_matches, stats.relocate_matches, stats.partial_matches);

fsd_diff_destroy(diff_ctx);

/* Apply a patch */
fsd_patch_ctx_t *patch_ctx = NULL;
fsd_patch_create(&patch_ctx, NULL);
fsd_patch_apply(patch_ctx, "source.img", "patch.bkdf", "output.img");
fsd_patch_destroy(patch_ctx);

/* Cleanup */
fsd_cleanup();
```

#### Advanced Options

```c
fsd_diff_options_t opts;
fsd_diff_options_init(&opts);

opts.block_size_log2 = 12;      // 4096-byte blocks
opts.search_radius = 8;          // Search ±8 blocks for relocations
opts.partial_threshold = 0.75f;  // Require 75% byte match for partial
opts.enable_identity = true;
opts.enable_relocation = true;
opts.enable_partial = true;
opts.verbose = false;
opts.force_scalar = false;       // Use SIMD if available

fsd_diff_ctx_t *ctx = NULL;
fsd_diff_create(&ctx, &opts);
```

#### Progress Callbacks

```c
void progress_callback(void *user_data, uint64_t current, uint64_t total) {
    int percent = (int)((current * 100) / total);
    printf("\rProgress: %d%%", percent);
    fflush(stdout);
}

fsd_diff_set_progress(diff_ctx, progress_callback, NULL);
```

#### Error Handling

```c
fsd_error_t err = fsd_diff_files(ctx, src, dest, patch);
if (err != FSD_SUCCESS) {
    fprintf(stderr, "Error: %s\n", fsd_strerror(err));
    return 1;
}
```

### Patch File Format

The BKDF (Block Diff Format) is documented in [PATCH_FORMAT_SPEC.md](PATCH_FORMAT_SPEC.md).

**Key features:**
- 32-byte header with stream lengths
- Three streams: operations, diff data, literal data
- Variable-length encoding for counts and offsets
- Dense and sparse diff formats with base-128 varint lengths
- Designed to compress well with xz/lzma

**Recommended workflow:**
```bash
# Create uncompressed patch
fsdiff delta source.img dest.img patch.bkdf

# Compress for distribution
xz -9 patch.bkdf

# Apply (decompress first)
xz -d patch.bkdf.xz
fsdiff patch source.img patch.bkdf output.img
```

## Code Structure

```
fsdiff/
├── include/fsdiff/          # Public API headers
│   ├── fsdiff.h            # Main umbrella header
│   ├── diff.h              # Diff generation API
│   ├── patch.h             # Patch application API
│   ├── types.h             # Core types and constants
│   ├── error.h             # Error codes
│   └── options.h           # Configuration structures
│
├── src/
│   ├── core/               # Core data structures
│   │   ├── block_tracker.c # Track block matching state
│   │   ├── hash_table.c    # CRC32 hash table (relocations)
│   │   ├── memory_pool.c   # Arena allocator for deltas
│   │   └── crc32.c         # CRC32 implementation
│   │
│   ├── stages/             # Three-stage matching pipeline
│   │   ├── stage_controller.c  # Orchestrates all stages
│   │   ├── stage_identity.c    # Same-position matching
│   │   ├── stage_relocation.c  # CRC32 relocated matching
│   │   └── stage_partial.c     # Local search partial matching
│   │
│   ├── encoding/           # BKDF format encoding/decoding
│   │   ├── bkdf_header.c       # File header I/O
│   │   └── operation_encoder.c # Encode operations and diffs
│   │
│   ├── simd/              # SIMD implementations
│   │   ├── simd_dispatch.c     # Runtime CPU detection
│   │   ├── projection_scalar.c # Scalar fallback
│   │   ├── projection_avx2.c   # AVX2 (x86-64)
│   │   └── projection_neon.c   # NEON (ARM)
│   │
│   ├── io/                # File I/O
│   │   ├── mmap_reader.c      # Memory-mapped input (cross-platform)
│   │   └── buffered_writer.c  # Buffered output
│   │
│   ├── platform.h         # Platform abstraction (Windows/Unix)
│   ├── diff.c             # Public diff API
│   └── patch.c            # Public patch API
│
├── tools/
│   └── fsdiff.c           # Command-line interface
│
└── tests/                 # Test suite (9 test programs)
    ├── test_simd.c        # SIMD implementation tests
    ├── test_decoder.c     # Hand-crafted format validation
    ├── test_roundtrip.c   # End-to-end integration tests
    └── ...                # Unit tests for core modules
```

## Coding Conventions

### Style
- **Indentation**: 4 spaces
- **Naming**: Snake case (`fsd_function_name`)
- **Prefix**: All public API uses `fsd_` prefix
- **Headers**: Include guards with `FSDIFF_MODULE_H` format

### Error Handling
- Functions return `fsd_error_t` error codes
- `FSD_SUCCESS` (0) on success
- Non-zero error codes defined in `include/fsdiff/error.h`
- Use `fsd_strerror()` to get error messages

### Memory Management
- Contexts use opaque handles (e.g., `fsd_diff_ctx_t*`)
- All allocations have corresponding destroy functions
- Memory pools for temporary allocations (deltas)

### Platform Portability
- Platform-specific code isolated in:
  - `src/io/mmap_reader.c` - File mapping
  - `src/platform.h` - Temp files and file operations
- Use `#ifdef _WIN32` for platform-specific code
- All other code is portable C99

## Testing

### Running Tests

```bash
cd build
ctest                    # Run all tests
ctest --verbose         # Verbose output
ctest --output-on-failure # Show failures only
```

### Test Coverage

| Test | Coverage |
|------|----------|
| `test_crc32` | CRC32 implementation |
| `test_memory_pool` | Arena allocator |
| `test_block_tracker` | Block state tracking |
| `test_simd` | SIMD implementations (AVX2/NEON/scalar) |
| `test_mmap_reader` | Memory-mapped I/O |
| `test_buffered_writer` | Buffered output |
| `test_bkdf_header` | File header encoding |
| `test_decoder` | Hand-crafted format validation (100% op coverage) |
| `test_roundtrip` | End-to-end integration tests |

### Continuous Integration

GitHub Actions CI tests on:
- Ubuntu x64 (with/without SIMD - AVX2)
- ARM (with/without SIMD - NEON)
- Windows x64 (with/without SIMD - AVX2)

All configurations must pass before merge.

## Algorithm Overview

### Three-Stage Matching Pipeline

1. **Stage 1: Identity Matching**
   - Compare blocks at same positions using `memcmp()`
   - Detect all-zero and all-one blocks via SIMD
   - Compute CRC32 for unmatched blocks

2. **Stage 2: Relocation Matching**
   - Build CRC32 hash table of source blocks
   - Look up unmatched dest blocks by CRC32
   - Verify matches with `memcmp()` (handle collisions)

3. **Stage 3: Partial Matching**
   - Local search: scan ±32KB offset range
   - SIMD-accelerated byte matching (`count_matches`)
   - Extend matches to adjacent blocks
   - Fallback: search relative to previous relocated block

### SIMD Optimizations

| Function | Scalar | AVX2 | NEON | Usage |
|----------|--------|------|------|-------|
| `is_zero` | 8-byte words | 32-byte vectors | 16-byte vectors | Identity stage |
| `is_one` | 8-byte words | 32-byte vectors | 16-byte vectors | Identity stage |
| `count_matches` | 8-byte XOR | popcount mask | horizontal sum | Partial stage |
