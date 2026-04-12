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
)

target_link_libraries(netudp_tests PRIVATE
    netudp
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(netudp_tests)
