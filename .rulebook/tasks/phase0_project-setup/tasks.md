## 1. Implementation
- [ ] 1.1 Create root CMakeLists.txt: cmake_minimum_required(3.20), project(netudp VERSION 0.1.0), set(CMAKE_CXX_STANDARD 17), add netudp STATIC library target, add install(TARGETS) rules
- [ ] 1.2 Create CMakePresets.json with presets: "debug" (Debug, default generator), "release" (Release), "relwithdebinfo", "cross-linux" (Zig CC, CMAKE_SYSTEM_NAME=Linux)
- [ ] 1.3 Create cmake/zig-toolchain.cmake: set CMAKE_C_COMPILER to "zig cc", CMAKE_CXX_COMPILER to "zig c++", verify cross-compile produces ELF binary from Windows
- [ ] 1.4 Add Google Test via FetchContent: FetchContent_Declare(googletest, GIT_TAG v1.14.0), FetchContent_MakeAvailable, add tests/ directory with tests/CMakeLists.txt, link GTest::gtest_main
- [ ] 1.5 Create .github/workflows/ci.yml: matrix strategy (windows-latest + MSVC, ubuntu-latest + GCC 12, macos-latest + AppleClang), steps: checkout → configure → build → test, cmake cache action
- [ ] 1.6 Create public header structure in include/netudp/: netudp.h (main lifecycle + send/recv stubs), netudp_types.h (error codes, NETUDP_OK/-1..-10, opaque handles, POD structs), netudp_config.h (compile flags: NETUDP_CRC32_ONLY, NETUDP_ENABLE_AVX512), netudp_buffer.h (buffer API forward decls), netudp_token.h (token gen forward decl)
- [ ] 1.7 Add .clang-tidy: enable checks modernize-*, readability-*, bugprone-*, performance-*, disable modernize-use-trailing-return-type
- [ ] 1.8 Add .clang-format: BasedOnStyle LLVM, ColumnLimit 100, IndentWidth 4, UseTab Never, AllowShortFunctionsOnASingleLine Inline
- [ ] 1.9 Implement netudp_init() in src/api.cpp: set global g_initialized flag, return NETUDP_OK; netudp_term(): clear flag, return void. Guard against double-init.
- [ ] 1.10 Create src/core/platform.h: platform detection macros (NETUDP_PLATFORM_WINDOWS, _LINUX, _MACOS), compiler macros (NETUDP_COMPILER_MSVC, _GCC, _CLANG), NETUDP_EXPORT/NETUDP_IMPORT for DLL visibility
- [ ] 1.11 Create tests/test_init.cpp: TEST(Lifecycle, InitTerm) verifying init returns OK, double-init returns OK, term after init succeeds
- [ ] 1.12 Create .gitignore entries: build/, build-*/, *.o, *.obj, *.lib, *.a, *.dll, *.so, *.dylib, CMakeCache.txt, CMakeFiles/

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update CHANGELOG.md with "## [0.1.0-dev] - Unreleased / Added: project skeleton, CMake build, CI pipeline"
- [ ] 2.2 Verify tests/test_init.cpp passes on all 3 platforms (or locally on Windows + WSL)
- [ ] 2.3 Run clang-tidy on src/api.cpp — zero warnings
