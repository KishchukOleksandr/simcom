# cmake/check_stack_usage.cmake
# Enforces Constitution Section 3 Stack Budget on GCC .su files:
#   - Simcom_FeedBytes (ISR helper): <= 32 Bytes
#   - All public API functions & internal helpers: <= 128 Bytes

file(GLOB_RECURSE SU_FILES "${BUILD_DIR}/CMakeFiles/simcom.dir/*.su")
if(NOT SU_FILES)
    message(STATUS "No .su files found (non-GNU compiler); skipping stack usage check.")
    return()
endif()

set(ISR_LIMIT 32)
set(API_LIMIT 128)
if(CMAKE_HOST_WIN32)
    # Microsoft x64 ABI mandates a 32-byte shadow space per call frame on Windows hosts
    math(EXPR ISR_LIMIT "${ISR_LIMIT} + 32")
    math(EXPR API_LIMIT "${API_LIMIT} + 32")
endif()

set(FAILED FALSE)
foreach(SU_FILE IN LISTS SU_FILES)
    file(STRINGS "${SU_FILE}" LINES)
    foreach(LINE IN LISTS LINES)
        string(REGEX MATCH "([^:\t]+)\t([0-9]+)\t([a-z]+)$" MATCHED "${LINE}")
        if(MATCHED)
            set(FUNC_NAME "${CMAKE_MATCH_1}")
            set(STACK_BYTES "${CMAKE_MATCH_2}")
            set(STACK_KIND "${CMAKE_MATCH_3}")

            if(NOT STACK_KIND STREQUAL "static")
                message(SEND_ERROR "Dynamic/unbounded stack allocation in ${FUNC_NAME}: ${STACK_KIND}")
                set(FAILED TRUE)
            endif()

            if(FUNC_NAME STREQUAL "Simcom_FeedBytes")
                if(STACK_BYTES GREATER ${ISR_LIMIT})
                    message(SEND_ERROR "ISR helper ${FUNC_NAME} stack usage ${STACK_BYTES}B exceeds ${ISR_LIMIT}B limit!")
                    set(FAILED TRUE)
                endif()
            else()
                if(STACK_BYTES GREATER ${API_LIMIT})
                    message(SEND_ERROR "Function ${FUNC_NAME} stack usage ${STACK_BYTES}B exceeds ${API_LIMIT}B limit!")
                    set(FAILED TRUE)
                endif()
            endif()
        endif()
    endforeach()
endforeach()

if(FAILED)
    message(FATAL_ERROR "Stack usage budget verification FAILED.")
else()
    message(STATUS "Stack usage budget verification PASSED (all functions <= 128B, Simcom_FeedBytes <= 32B).")
endif()
