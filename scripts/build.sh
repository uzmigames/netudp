#!/usr/bin/env bash
set -euo pipefail

# netudp build script
# Usage:
#   ./scripts/build.sh              # debug build + tests
#   ./scripts/build.sh release      # release build + tests
#   ./scripts/build.sh clean        # remove all build dirs
#   ./scripts/build.sh test         # run tests only (assumes already built)
#   ./scripts/build.sh bench        # release build + benchmarks
#   ./scripts/build.sh cross-linux  # cross-compile for Linux via Zig CC
#   ./scripts/build.sh all          # debug + release + tests

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PRESET="${1:-debug}"
JOBS="${NETUDP_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

case "$PRESET" in
    debug)
        echo "=== Configure (debug, Zig CC) ==="
        cmake --preset debug
        echo "=== Build ==="
        cmake --build build-debug -j "$JOBS"
        echo "=== Test ==="
        ctest --test-dir build-debug --output-on-failure
        echo "=== Done: build-debug/libnetudp.a ==="
        ;;

    release)
        echo "=== Configure (release, Zig CC) ==="
        cmake --preset release
        echo "=== Build ==="
        cmake --build build-release -j "$JOBS"
        echo "=== Test ==="
        ctest --test-dir build-release --output-on-failure
        echo "=== Done: build-release/libnetudp.a ==="
        ;;

    bench)
        echo "=== Configure (release + bench, Zig CC) ==="
        cmake --preset release
        echo "=== Build ==="
        cmake --build build-release -j "$JOBS"
        echo "=== Benchmarks ==="
        if [ -f build-release/bench/bench_pps ]; then
            ./build-release/bench/bench_pps
        elif [ -f build-release/bench/bench_pps.exe ]; then
            ./build-release/bench/bench_pps.exe
        else
            echo "No benchmarks found (bench/ targets not yet implemented)"
        fi
        ;;

    test)
        if [ -d build-debug ]; then
            echo "=== Test (debug) ==="
            ctest --test-dir build-debug --output-on-failure
        elif [ -d build-release ]; then
            echo "=== Test (release) ==="
            ctest --test-dir build-release --output-on-failure
        else
            echo "No build directory found. Run: ./scripts/build.sh debug"
            exit 1
        fi
        ;;

    cross-linux)
        echo "=== Configure (cross-linux, Zig CC) ==="
        cmake --preset cross-linux
        echo "=== Build ==="
        cmake --build build-linux -j "$JOBS"
        echo "=== Done: build-linux/libnetudp.a (ELF) ==="
        file build-linux/libnetudp.a 2>/dev/null || true
        ;;

    clean)
        echo "=== Cleaning all build directories ==="
        rm -rf build-debug build-release build-reldbg build-linux build-msvc build
        echo "=== Done ==="
        ;;

    all)
        "$0" debug
        "$0" release
        ;;

    *)
        echo "Usage: $0 {debug|release|bench|test|cross-linux|clean|all}"
        exit 1
        ;;
esac
