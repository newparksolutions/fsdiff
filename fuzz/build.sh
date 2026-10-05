#!/usr/bin/env bash
# Build the fsdiff libFuzzer harnesses.
#
# Usage: fuzz/build.sh [patch] [full]     (default: both)
#
# Configures the unmodified project with clang + sanitizers, per target:
#   patch  build-fuzz-patchonly/  FSDIFF_BUILD_PATCH_ONLY=ON  -> fuzz_patch
#   full   build-fuzz-full/       full library                -> fuzz_roundtrip, gen_seeds
# Binaries land in <build dir>/fuzz/. Building one target leaves the other's
# binaries alone, so a running fork-mode fuzzer (which re-executes its binary
# for every job) is not swapped underneath.
#
# Environment:
#   CC                    compiler (default: clang)
#   FUZZ_GCC_INSTALL_DIR  GCC installation whose libstdc++ libFuzzer links
#                         against (default: newest one that has libstdc++.so)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-clang}"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
CFLAGS_COMMON="-g -fno-omit-frame-pointer $SAN"
# Library only: leave the differ's match-counting kernels uninstrumented.
IGNORE="-fsanitize-ignorelist=$ROOT/fuzz/ignorelist.txt -fsanitize-coverage-ignorelist=$ROOT/fuzz/ignorelist.txt"

# clang picks the newest GCC installation even when its libstdc++ development
# files are missing, which breaks the libFuzzer link; pick one that has them.
if [[ -z "${FUZZ_GCC_INSTALL_DIR:-}" ]]; then
    for d in $(ls -d /usr/lib/gcc/x86_64-linux-gnu/* /usr/lib/gcc/aarch64-linux-gnu/* \
                   2>/dev/null | sort -V -r); do
        if [[ -e "$d/libstdc++.so" ]]; then FUZZ_GCC_INSTALL_DIR="$d"; break; fi
    done
fi
LINK_EXTRA=""
if [[ -n "${FUZZ_GCC_INSTALL_DIR:-}" ]]; then
    LINK_EXTRA="--gcc-install-dir=$FUZZ_GCC_INSTALL_DIR"
fi

build_lib() {  # <build dir> <patch-only ON|OFF>
    local dir="$1" po="$2"
    # LINK_EXTRA is passed here too so CMake's compiler check links with the
    # same GCC installation as the harnesses.
    cmake -S "$ROOT" -B "$dir" \
        -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$CFLAGS_COMMON $LINK_EXTRA -fsanitize=fuzzer-no-link $IGNORE" \
        -DFSDIFF_BUILD_PATCH_ONLY="$po" \
        -DFSDIFF_BUILD_SHARED=OFF \
        -DFSDIFF_BUILD_STATIC=ON \
        -DFSDIFF_BUILD_CLI=OFF \
        -DFSDIFF_BUILD_TESTS=OFF >/dev/null
    cmake --build "$dir" --parallel >/dev/null
    mkdir -p "$dir/fuzz"
}

link() {  # <build dir> <output name> <source> <extra flags...>
    local dir="$1" out="$2" src="$3"; shift 3
    "$CC" $CFLAGS_COMMON $LINK_EXTRA "$@" \
        -I"$ROOT/include" -I"$dir/include" \
        "$ROOT/fuzz/$src" "$dir/lib/libfsdiff.a" -lm \
        -o "$dir/fuzz/$out"
    echo "built $dir/fuzz/$out"
}

TARGETS=("$@")
[[ ${#TARGETS[@]} -eq 0 ]] && TARGETS=(patch full)

for t in "${TARGETS[@]}"; do
    case "$t" in
    patch)
        dir="$ROOT/build-fuzz-patchonly"
        build_lib "$dir" ON
        link "$dir" fuzz_patch fuzz_patch.c -fsanitize=fuzzer
        ;;
    full)
        dir="$ROOT/build-fuzz-full"
        build_lib "$dir" OFF
        link "$dir" fuzz_roundtrip fuzz_roundtrip.c -fsanitize=fuzzer
        link "$dir" gen_seeds gen_seeds.c
        ;;
    *)
        echo "unknown target: $t (expected patch or full)" >&2
        exit 2
        ;;
    esac
done
