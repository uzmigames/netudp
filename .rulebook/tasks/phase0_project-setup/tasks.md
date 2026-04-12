## 1. Implementation
- [x] 1.1 Create root CMakeLists.txt: cmake_minimum_required(3.20), project(netudp VERSION 0.1.0), set(CMAKE_CXX_STANDARD 17), add netudp STATIC library target, add install(TARGETS) rules
- [x] 1.2 Create CMakePresets.json with presets: "debug" (Debug, default generator), "release" (Release), "relwithdebinfo", "cross-linux" (Zig CC, CMAKE_SYSTEM_NAME=Linux)
- [x] 1.3 Create cmake/zig-toolchain.cmake: set CMAKE_C_COMPILER to "zig cc", CMAKE_CXX_COMPILER to "zig c++", verify cross-compile produces ELF binary from Windows
- [x] 1.4 Add Google Test via FetchContent: FetchContent_Declare(googletest, GIT_TAG v1.14.0), FetchContent_MakeAvailable, add tests/ directory with tests/CMakeLists.txt, link GTest::gtest_main
- [x] 1.5 Create .github/workflows/ci.yml: matrix strategy (windows-latest + MSVC, ubuntu-latest + GCC 12, macos-latest + AppleClang), steps: checkout → configure → build → test, cmake cache action
- [x] 1.6 Create public header structure in include/netudp/: netudp.h (main lifecycle + send/recv stubs), netudp_types.h (error codes, NETUDP_OK/-1..-10, opaque handles, POD structs), netudp_config.h (compile flags: NETUDP_CRC32_ONLY, NETUDP_ENABLE_AVX512), netudp_buffer.h (buffer API forward decls), netudp_token.h (token gen forward decl)
- [x] 1.7 Add .clang-tidy: enable checks modernize-*, readability-*, bugprone-*, performance-*, disable modernize-use-trailing-return-type
- [x] 1.8 Add .clang-format: BasedOnStyle LLVM, ColumnLimit 100, IndentWidth 4, UseTab Never, AllowShortFunctionsOnASingleLine Inline
- [x] 1.9 Implement netudp_init() in src/api.cpp: set global g_initialized flag, return NETUDP_OK; netudp_term(): clear flag, return void. Guard against double-init.
- [x] 1.10 Create src/core/platform.h: platform detection macros (NETUDP_PLATFORM_WINDOWS, _LINUX, _MACOS), compiler macros (NETUDP_COMPILER_MSVC, _GCC, _CLANG), NETUDP_EXPORT/NETUDP_IMPORT for DLL visibility
- [x] 1.11 Create tests/test_init.cpp: TEST(Lifecycle, InitTerm) verifying init returns OK, double-init returns OK, term after init succeeds
- [x] 1.12 .gitignore already has build/, build-*/, *.o, *.obj, *.lib, etc. from rulebook setup

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update CHANGELOG.md with "## [0.1.0-dev] - Unreleased / Added: project skeleton, CMake build, CI pipeline"
- [x] 2.2 Verify tests/test_init.cpp passes: 6/6 tests passed on Windows/MSVC
- [x] 2.3 Run clang-tidy on src/api.cpp — zero warnings (fixed 0.0f → 0.0F uppercase literal suffix)
