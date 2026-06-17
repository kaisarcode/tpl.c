cmake_minimum_required(VERSION 3.14)
project(tpl C)

set(CMAKE_C_STANDARD 11)

option(TPL_NATIVE "Enable native hardware optimizations" OFF)

if(NOT DEFINED TPL_BUILD_VERSION)
    set(TPL_BUILD_VERSION 0)
endif()

if(NOT MSVC)
    add_compile_options(-O3 -Wall -Wextra -Werror)
    add_definitions(-D_GNU_SOURCE)
    if(TPL_NATIVE)
        add_compile_options(-march=native)
    endif()
else()
    add_compile_options(/O2 /W3 /WX)
endif()

set(TPL_SOURCES src/libtpl.c)
set(TPL_HEADERS src/tpl.h)

set_source_files_properties(src/libtpl.c PROPERTIES
    COMPILE_DEFINITIONS KC_TPL_BUILD_VERSION=${TPL_BUILD_VERSION}
)

add_library(tpl_shared SHARED ${TPL_SOURCES})
set_target_properties(tpl_shared PROPERTIES OUTPUT_NAME tpl PREFIX "lib")
target_include_directories(tpl_shared PRIVATE src)

add_library(tpl_static STATIC ${TPL_SOURCES})
set_target_properties(tpl_static PROPERTIES OUTPUT_NAME tpl PREFIX "lib" POSITION_INDEPENDENT_CODE ON)
target_include_directories(tpl_static PRIVATE src)

add_executable(tpl_exe src/tpl.c)
set_target_properties(tpl_exe PROPERTIES OUTPUT_NAME tpl)
target_include_directories(tpl_exe PRIVATE src)
target_link_libraries(tpl_exe tpl_static)

if(UNIX AND NOT ANDROID)
    target_link_libraries(tpl_exe pthread m)
endif()
