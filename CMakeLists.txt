cmake_minimum_required(VERSION 3.14)
project(tpl C)

set(CMAKE_C_STANDARD 11)

option(TPL_NATIVE "Enable native hardware optimizations" OFF)
option(TPL_BUILD_TESTS "Build tpl tests" OFF)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc")
endif()

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

if(TPL_BUILD_TESTS)
    enable_testing()

    if(TPL_TEST_SHARED_LIBRARY)
        add_library(tpl_test_shared SHARED IMPORTED)
        set_target_properties(tpl_test_shared PROPERTIES
            IMPORTED_LOCATION "${TPL_TEST_SHARED_LIBRARY}"
        )
        if(TPL_TEST_IMPORT_LIBRARY)
            set_target_properties(tpl_test_shared PROPERTIES
                IMPORTED_IMPLIB "${TPL_TEST_IMPORT_LIBRARY}"
            )
        endif()
        set(TPL_TEST_SHARED_TARGET tpl_test_shared)
    else()
        set(TPL_TEST_SHARED_TARGET tpl_shared)
    endif()

    add_executable(tpl_contract_test src/test.c)
    target_include_directories(tpl_contract_test PRIVATE src)
    target_link_libraries(tpl_contract_test ${TPL_TEST_SHARED_TARGET})
    if(UNIX AND NOT ANDROID)
        target_link_libraries(tpl_contract_test pthread m)
    endif()

    add_custom_command(TARGET tpl_contract_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${TPL_TEST_SHARED_TARGET}>
            $<TARGET_FILE_DIR:tpl_contract_test>
    )

    add_test(NAME library-kc_tpl_options_default COMMAND tpl_contract_test kc_tpl_options_default)
    add_test(NAME library-kc_tpl_options_load_env COMMAND tpl_contract_test kc_tpl_options_load_env)
    add_test(NAME library-kc_tpl_options_free COMMAND tpl_contract_test kc_tpl_options_free)
    add_test(NAME library-kc_tpl_stop COMMAND tpl_contract_test kc_tpl_stop)
    add_test(NAME library-kc_tpl_on_signal COMMAND tpl_contract_test kc_tpl_on_signal)
    add_test(NAME library-kc_tpl_raise_signal COMMAND tpl_contract_test kc_tpl_raise_signal)
    add_test(NAME library-kc_tpl_listen_signals COMMAND tpl_contract_test kc_tpl_listen_signals)
    add_test(NAME library-kc_tpl_listen_signal COMMAND tpl_contract_test kc_tpl_listen_signal)
    add_test(NAME library-kc_tpl_signal_listener COMMAND tpl_contract_test kc_tpl_signal_listener)
    add_test(NAME library-kc_tpl_version COMMAND tpl_contract_test kc_tpl_version)
    add_test(NAME library-kc_tpl_open COMMAND tpl_contract_test kc_tpl_open)
    add_test(NAME library-kc_tpl_close COMMAND tpl_contract_test kc_tpl_close)
    add_test(NAME library-kc_tpl_set_root COMMAND tpl_contract_test kc_tpl_set_root)
    add_test(NAME library-kc_tpl_set_var COMMAND tpl_contract_test kc_tpl_set_var)
    add_test(NAME library-kc_tpl_render_string COMMAND tpl_contract_test kc_tpl_render_string)
    add_test(NAME library-kc_tpl_strerror COMMAND tpl_contract_test kc_tpl_strerror)
endif()
