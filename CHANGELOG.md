# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0-dev] - Unreleased

### Added
- Project skeleton: CMake 3.20+, C++17, static library target
- CMakePresets.json (debug, release, relwithdebinfo, cross-linux via Zig CC)
- Zig CC cross-compilation toolchain (cmake/zig-toolchain.cmake)
- Google Test via FetchContent (v1.14.0)
- GitHub Actions CI (Windows/MSVC, Linux/GCC, macOS/Clang)
- Public header structure: netudp.h, netudp_types.h, netudp_config.h, netudp_buffer.h, netudp_token.h
- `netudp_init()` / `netudp_term()` lifecycle with idempotent double-init/term
- `netudp_simd_level()` query (returns Generic until Phase 1 SIMD detection)
- Platform detection macros (src/core/platform.h)
- clang-tidy and clang-format configuration
- All public API function declarations with link stubs
