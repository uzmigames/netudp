cmake_minimum_required(VERSION 3.20)

project(netudp
    VERSION 0.1.0
    DESCRIPTION "High-performance, zero-GC UDP networking library for game servers"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(NETUDP_BUILD_TESTS "Build test suite" ON)
option(NETUDP_BUILD_BENCH "Build benchmarks" OFF)
option(NETUDP_BUILD_EXAMPLES "Build examples" OFF)
option(NETUDP_ENABLE_AVX512 "Enable AVX-512 SIMD (experimental)" OFF)
option(NETUDP_CRC32_ONLY "CRC32C-only mode for LAN/dev (no AEAD encryption)" OFF)

# Export compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- netudp static library ---
add_library(netudp STATIC
    src/api.cpp
)

target_include_directories(netudp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(netudp PRIVATE
    $<$<BOOL:${NETUDP_CRC32_ONLY}>:NETUDP_CRC32_ONLY=1>
    $<$<BOOL:${NETUDP_ENABLE_AVX512}>:NETUDP_ENABLE_AVX512=1>
)

# Platform-specific link libraries
if(WIN32)
    target_link_libraries(netudp PRIVATE ws2_32)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(netudp PRIVATE /W4 /WX)
else()
    target_compile_options(netudp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# --- Alias for FetchContent consumers ---
add_library(netudp::netudp ALIAS netudp)

# --- Tests ---
if(NETUDP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Benchmarks ---
if(NETUDP_BUILD_BENCH)
    add_subdirectory(bench)
endif()

# --- Examples ---
if(NETUDP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Install ---
include(GNUInstallDirs)

install(TARGETS netudp
    EXPORT netudpTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/netudp/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/netudp
)

install(EXPORT netudpTargets
    FILE netudpTargets.cmake
    NAMESPACE netudp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/netudp
)
