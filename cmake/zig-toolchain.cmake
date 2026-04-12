# Zig CC cross-compilation toolchain for CMake
# Usage: cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake

set(CMAKE_C_COMPILER "zig" CACHE STRING "")
set(CMAKE_CXX_COMPILER "zig" CACHE STRING "")

set(CMAKE_C_COMPILER_ARG1 "cc")
set(CMAKE_CXX_COMPILER_ARG1 "c++")

# Zig handles sysroot and target triple internally.
# Set CMAKE_SYSTEM_NAME in the preset or command line:
#   -DCMAKE_SYSTEM_NAME=Linux   (cross-compile to Linux)
#   -DCMAKE_SYSTEM_NAME=Windows (native on Windows)
