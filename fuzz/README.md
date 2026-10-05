# Fuzzing fsdiff

libFuzzer harnesses for the patcher. The patch file is untrusted input, so
`fsd_patch_apply()` is the main target. The harnesses use only the public
API and are built against the unmodified library.

## Harnesses

| Binary | Library config | What it checks |
|---|---|---|
| `fuzz_patch` | patch-only (the shipped config) | Memory safety and UB in the decoder, header parsing and source/patch readers. On `FSD_SUCCESS` the output must be exactly `dest_blocks << block_size_log2` bytes |
| `fuzz_roundtrip` | full | Correctness: diffs a fuzz-generated src/dest pair with fuzzed options, applies the patch, and aborts unless the output equals dest. Any diff failure is also treated as a finding |

All files (source, dest, patch, output) are memfds passed to the library as
`/proc/self/fd/N`, so the real open/mmap/fopen paths run without disk I/O.
The differ's temporary stream files are an exception: `fsd_create_temp_file`
creates them under `/tmp`.

### `fuzz_patch` input format

Byte 0 is a control byte. Bit 0 selects the mode and bits 1-3 select one of
eight source images, all prefixes of one non-repeating pattern: 0, 31, 4103,
64 Ki, 256 Ki+1, 1 Mi, 1 Mi+4097 and 16 Mi+1 bytes.

* **Raw mode** (bit 0 clear): the remaining bytes are the patch file
  verbatim. This tests header validation.
* **Structured mode** (bit 0 set): the next 7 bytes give the block size
  (32 B to 16 MiB, the full range the format allows), `dest_blocks`,
  `op_stream_len` and `diff_stream_len`. The harness writes a header with
  stream lengths consistent with the data, so mutations reach the op decoder
  instead of being rejected by the stream-length checks.

Output is capped at 16 MiB. Raw-mode inputs whose header declares more are
skipped. This is a harness limit (memfds live in RAM), not a library
requirement: output amplification is expected and bounded in deployment by
the target partition size.

### `fuzz_roundtrip` input format

Described in the header comment of `fuzz_roundtrip.c`. In short: block size
32 B to 4 KiB, stage enable flags, partial threshold, search radius,
force_scalar, a source (a seeded pattern, optionally with its first 4 KiB
taken from the input; up to 1 MiB, aligned or not), up to 128 dest blocks
(512 KiB), then an edit script that builds dest. The script ops are:
same-offset copy, copy from an arbitrary offset, zero/0xFF runs, literal
bytes, nearby copy with byte edits, repeats of earlier dest content, and
block-scale shifted copies.

The harness only generates inputs the differ accepts: images are never empty,
and relocation is only enabled together with identity.

The differ's partial search is a brute-force sweep costing up to about
dest_blocks × (2 × radius + 1) × block_size² byte comparisons. Left
unbounded, fuzzed settings took 35-55 s per input (radius 126 at 2 KiB
blocks) and dominated the run. When the partial stage is on, the harness
reduces `dest_blocks` to keep that worst case within `FZ_PARTIAL_BUDGET`
(2^30, about 0.3 s uninstrumented). Large block sizes with wide windows
therefore get small images.

## Building

```sh
fuzz/build.sh [patch] [full]    # default: both
```

The script configures the project once per target with clang and
`-fsanitize=address,undefined` (UB is fatal), producing:

* `patch`: `build-fuzz-patchonly/fuzz/fuzz_patch`
* `full`: `build-fuzz-full/fuzz/fuzz_roundtrip` and `build-fuzz-full/fuzz/gen_seeds`

The fuzz builds are separate from the main build. The top-level CMake project
does not reference `fuzz/`, and nothing here changes how it or CI builds.

Building one target leaves the other target's binaries alone. This matters
because a fork-mode fuzzer re-executes its binary for every job. Rebuilding a
running target's binary is a way to update it without a restart.

`ignorelist.txt` excludes the differ's SIMD/scalar match-counting kernels
from sanitizer and coverage instrumentation. They take ~90% of
`fuzz_roundtrip`'s time and instrumenting them makes it about 7x slower. The
patcher does not use them.

On Ubuntu with clang 18, clang picks GCC 14's directory even when only an
older GCC has libstdc++ development files installed, and the libFuzzer link
fails with `cannot find -lstdc++`. `build.sh` works around this by passing
`--gcc-install-dir` for the newest GCC that has `libstdc++.so`. Override it
with `FUZZ_GCC_INSTALL_DIR`.

## Running

```sh
mkdir -p corpus
build-fuzz-full/fuzz/gen_seeds corpus       # corpus/patch, corpus/roundtrip

build-fuzz-patchonly/fuzz/fuzz_patch -dict=fuzz/bkdf.dict \
    -max_len=262144 -timeout=10 -rss_limit_mb=1024 corpus/patch

build-fuzz-full/fuzz/fuzz_roundtrip \
    -max_len=65536 -timeout=30 -rss_limit_mb=1024 corpus/roundtrip
```

For long unattended runs, add `-fork=N -ignore_crashes=1 -ignore_timeouts=1
-ignore_ooms=1 -max_total_time=<seconds>`. Fork mode runs N worker processes,
keeps going after a finding and merges new inputs back into the corpus
directory. Crashes are written as `crash-*` files in the current directory
(or under `-artifact_prefix`). Reproduce one by passing it as the only
argument:

```sh
build-fuzz-patchonly/fuzz/fuzz_patch crash-<hash>
```

To check coverage of a corpus, run
`fuzz_patch -runs=0 -print_coverage=1 corpus/patch` and grep for `src/patch.c`.

Approximate total speeds with `-fork=3` on an 8-core WSL2 machine:
`fuzz_patch` ~1,500-5,000 runs/s and `fuzz_roundtrip` ~25-60 runs/s,
depending on image sizes.

## Not covered

* **O_DIRECT source reads.** memfds don't support O_DIRECT, so the
  block-device path of `source_reader` (unaligned reads through the bounce
  buffer) is not exercised. This needs a separate harness on a real ext4
  file or loop device.
* **fsmap stage.** It only activates when both images are ext4, which the
  round-trip generator does not produce. Seeding the corpus with tiny ext4
  images would cover it.
* **32-bit builds.** Worth running the patch harness under `-m32` if the
  patcher ships to 32-bit targets. From reading the code (not yet confirmed
  by a run), `read_varint` in `src/patch.c` shifts a `size_t` by up to 63
  bits, which is UB when `size_t` is 32 bits.
