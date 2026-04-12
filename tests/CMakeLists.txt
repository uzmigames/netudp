include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# Prevent GoogleTest from overriding our compiler/linker options on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# --- Test executable ---
add_executable(netudp_tests
    test_init.cpp
    test_simd.cpp
    test_containers.cpp
    test_address.cpp
    test_socket.cpp
    test_echo_integration.cpp
    test_crypto.cpp
    test_replay.cpp
    test_packet_crypto.cpp
    test_coverage_gaps.cpp
    test_connect_token.cpp
)

# Tests need access to internal src/ headers for SIMD dispatch testing
target_include_directories(netudp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(netudp_tests PRIVATE
    netudp
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(netudp_tests)
