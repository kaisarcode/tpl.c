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

    add_test(NAME library-options COMMAND tpl_contract_test options)
    add_test(NAME library-context COMMAND tpl_contract_test context)
    add_test(NAME library-signals COMMAND tpl_contract_test signals)
    add_test(NAME library-render-core COMMAND tpl_contract_test render-core)
    add_test(NAME library-include COMMAND tpl_contract_test include)
    add_test(NAME library-failure-paths COMMAND tpl_contract_test failure-paths)

    if(TPL_TEST_CLI)
        add_executable(tpl_test_cli IMPORTED)
        set_target_properties(tpl_test_cli PROPERTIES
            IMPORTED_LOCATION "${TPL_TEST_CLI}"
        )
        set(TPL_TEST_CLI_COMMAND tpl_test_cli)
    else()
        set(TPL_TEST_CLI_COMMAND tpl_exe)
    endif()

    add_test(NAME cli-help COMMAND ${TPL_TEST_CLI_COMMAND} --help)
    add_test(NAME cli-version COMMAND ${TPL_TEST_CLI_COMMAND} --version)
    add_test(NAME cli-invalid-flag COMMAND ${TPL_TEST_CLI_COMMAND} --invalid-flag)
    set_tests_properties(cli-invalid-flag PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli-missing-root COMMAND ${TPL_TEST_CLI_COMMAND} --root)
    set_tests_properties(cli-missing-root PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli-missing-var COMMAND ${TPL_TEST_CLI_COMMAND} --var)
    set_tests_properties(cli-missing-var PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cli-invalid-var COMMAND ${TPL_TEST_CLI_COMMAND} --var bad)
    set_tests_properties(cli-invalid-var PROPERTIES WILL_FAIL TRUE)
endif()
