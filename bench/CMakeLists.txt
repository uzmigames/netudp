add_executable(netudp_bench
    bench_main.cpp
    bench_pps.cpp
    bench_latency.cpp
    bench_simd_compare.cpp
    bench_scalability.cpp
    bench_memory.cpp
    bench_coalescing.cpp
    bench_throughput.cpp
    bench_crypto.cpp
)

target_include_directories(netudp_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(netudp_bench PRIVATE netudp)

if(WIN32)
    target_link_libraries(netudp_bench PRIVATE Psapi)
endif()

target_compile_definitions(netudp_bench PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>
)
