cmake_minimum_required(VERSION 3.20)
project(libsimcom VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SIMCOM_BUILD_TESTS "Build libsimcom host unit and chaos test suites" ON)

set(SIMCOM_WARNING_FLAGS
    -Wall
    -Wextra
    -Werror
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wundef
    -Wcast-qual
    -fno-common
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND SIMCOM_WARNING_FLAGS -fstack-usage)
endif()

add_library(simcom STATIC
    src/simcom.c
    src/simcom_ringbuf.c
    src/simcom_parser.c
    src/simcom_lexer.c
    src/simcom_fsm.c
    src/simcom_log.c
    src/profiles/simcom_profile_a76xx.c
)

target_include_directories(simcom
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/internal
)

target_compile_options(simcom PRIVATE ${SIMCOM_WARNING_FLAGS})

add_custom_target(check_stack_usage
    COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_stack_usage.cmake
    DEPENDS simcom
    COMMENT "Verifying Constitution Section 3 stack usage budgets (.su)"
)

if(SIMCOM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_test(
        NAME test_stack_budget
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_stack_usage.cmake
    )
endif()

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API and architecture documentation with Doxygen"
        VERBATIM
    )
endif()

