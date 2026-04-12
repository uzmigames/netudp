# Proposal: Project Setup (v0.1.0 Foundation)

## Why
The netudp library needs a solid build system, CI pipeline, test harness, and public header structure before any implementation can begin. Every subsequent phase depends on CMake building correctly, tests running on all 3 platforms, and the public API headers being in place. This is the "zero to compilable skeleton" phase.

## What Changes
- Root CMakeLists.txt with CMake 3.20+ presets for Debug/Release/Cross
- Zig CC toolchain integration for cross-compilation (Windows→Linux, etc.)
- Google Test fetched via FetchContent for unit testing
- GitHub Actions CI matrix: Windows (MSVC), Linux (GCC), macOS (Clang)
- Public header directory structure (`include/netudp/`)
- clang-tidy and clang-format configuration for code quality
- `netudp_init()` / `netudp_term()` lifecycle stubs as the first extern "C" API

## Impact
- Affected specs: 00-project-setup
- Affected code: CMakeLists.txt, cmake/, include/netudp/, src/api.cpp, .github/workflows/, .clang-tidy, .clang-format
- Breaking change: NO (greenfield)
- User benefit: Reproducible builds on all platforms from day one
