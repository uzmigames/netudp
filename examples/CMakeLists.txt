cmake_minimum_required(VERSION 3.20)

# Each example is a standalone C executable.
foreach(example echo_server echo_client chat_server stress_test)
    add_executable(${example} ${example}.c)
    target_link_libraries(${example} PRIVATE netudp)
    if(WIN32)
        target_compile_definitions(${example} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()
