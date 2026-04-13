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
option(NETUDP_DISABLE_NETC "Disable netc compression (not recommended)" OFF)
option(NETUDP_ENABLE_TRACY "Integrate Tracy profiler (requires network access)" OFF)
option(NETUDP_DISABLE_PROFILING "Compile out built-in profiler (zero overhead)" OFF)
option(NETUDP_ENABLE_IO_URING "Enable io_uring socket backend (Linux 5.7+)" OFF)

# --- io_uring detection (Linux only) ---
if(NETUDP_ENABLE_IO_URING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(LIBURING_LIB uring)
    find_path(LIBURING_INCLUDE liburing.h)
    if(LIBURING_LIB AND LIBURING_INCLUDE)
        set(NETUDP_HAS_IO_URING TRUE)
        message(STATUS "io_uring: found (${LIBURING_LIB})")
    else()
        message(WARNING "io_uring: liburing not found, falling back to recvmmsg/sendmmsg")
        set(NETUDP_HAS_IO_URING FALSE)
    endif()
else()
    set(NETUDP_HAS_IO_URING FALSE)
endif()

# --- Tracy profiler (optional) ---
if(NETUDP_ENABLE_TRACY)
    include(FetchContent)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        v0.11.1
        GIT_SHALLOW    TRUE
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
    set(NETUDP_HAS_TRACY TRUE)
endif()

# --- netc compression library (default ON) ---
if(NOT NETUDP_DISABLE_NETC)
    include(FetchContent)
    FetchContent_Declare(
        netc
        GIT_REPOSITORY https://github.com/uzmigames/netc.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(netc)
    set(NETUDP_HAS_NETC TRUE)
endif()

# Export compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- netudp static library ---
add_library(netudp STATIC
    src/api.cpp
    src/server.cpp
    src/client.cpp
    src/sim/network_sim.cpp
    src/core/address.cpp
    src/connection/connect_token.cpp
    src/socket/socket.cpp
    src/socket/socket_uring.cpp
    src/crypto/vendor/monocypher.c
    src/crypto/aead.cpp
    src/crypto/xchacha.cpp
    src/crypto/crc32c.cpp
    src/crypto/random.cpp
    src/crypto/packet_crypto.cpp
    src/simd/netudp_simd_detect.cpp
    src/simd/simd_generic.cpp
    src/simd/simd_sse42.cpp
    src/simd/simd_avx2.cpp
    src/simd/simd_neon.cpp
    src/profiling/profiler.cpp
)

# --- Per-file SIMD compile flags (non-MSVC only, MSVC enables intrinsics by default) ---
if(NOT MSVC)
    set_source_files_properties(src/simd/simd_sse42.cpp PROPERTIES COMPILE_FLAGS "-msse4.2 -mpopcnt")
    set_source_files_properties(src/simd/simd_avx2.cpp PROPERTIES COMPILE_FLAGS "-mavx2 -mbmi2")
endif()

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
    $<$<BOOL:${NETUDP_HAS_NETC}>:NETUDP_HAS_NETC=1>
    $<$<BOOL:${NETUDP_HAS_TRACY}>:NETUDP_ENABLE_TRACY=1>
    $<$<BOOL:${NETUDP_DISABLE_PROFILING}>:NETUDP_DISABLE_PROFILING=1>
    $<$<BOOL:${NETUDP_HAS_IO_URING}>:NETUDP_HAS_IO_URING=1>
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>
)

# Platform-specific link libraries
if(WIN32)
    target_link_libraries(netudp PRIVATE ws2_32 bcrypt)
endif()

# io_uring link
if(NETUDP_HAS_IO_URING)
    target_include_directories(netudp PRIVATE ${LIBURING_INCLUDE})
    target_link_libraries(netudp PRIVATE ${LIBURING_LIB})
endif()

# Tracy link
if(NETUDP_HAS_TRACY)
    target_link_libraries(netudp PUBLIC TracyClient)
endif()

# netc compression (default ON)
if(NETUDP_HAS_NETC)
    target_link_libraries(netudp PRIVATE netc)
    target_include_directories(netudp PRIVATE ${netc_SOURCE_DIR}/include)

    # Fix: Zig CC needs -mxsave for __builtin_ia32_xgetbv in netc SIMD detection
    if(NOT MSVC AND TARGET netc)
        set_source_files_properties(
            ${netc_SOURCE_DIR}/src/simd/netc_simd_generic.c
            TARGET_DIRECTORY netc
            PROPERTIES COMPILE_FLAGS "-mxsave"
        )
        # Suppress -Werror on vendor code (unused static functions in netc)
        target_compile_options(netc PRIVATE -Wno-error -Wno-unused-function)
    endif()
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(netudp PRIVATE /W4 /WX /wd4324)  # C4324: struct padded due to alignas
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
option(NETUDP_BUILD_EXAMPLES "Build example programs" OFF)
if(NETUDP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Examples ---
if(NETUDP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Install ---
include(GNUInstallDirs)

install(TARGETS netudp
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/netudp/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/netudp
)
