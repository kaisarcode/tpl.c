cmake_minimum_required(VERSION 3.14)
project(tpl C)

set(CMAKE_C_STANDARD 11)


option(TPL_NATIVE "Enable native hardware optimizations" OFF)

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    if(TPL_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_definitions(-D_GNU_SOURCE)
else()
    add_compile_options(/O2 /W3 /WX)
endif()

# Main executable
add_executable(tpl
    src/tpl.c
)

target_include_directories(tpl PRIVATE src)

if(UNIX AND NOT ANDROID)
    target_link_libraries(tpl pthread m)
endif()
