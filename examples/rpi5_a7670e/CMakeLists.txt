# examples/rpi5_a7670e/CMakeLists.txt
# Builds the portable application coordinator library (all hosts) and the
# Raspberry Pi 5 Linux executable (when targeting Linux).

add_library(simcom_rpi5_app STATIC
    rpi5_app.c
)

target_include_directories(simcom_rpi5_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(simcom_rpi5_app
    PUBLIC
        simcom
)

target_compile_options(simcom_rpi5_app PRIVATE ${SIMCOM_WARNING_FLAGS})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(simcom_rpi5_a7670e
        main.c
        rpi5_platform_linux.c
    )

    target_compile_definitions(simcom_rpi5_a7670e PRIVATE _POSIX_C_SOURCE=200809L)
    target_compile_options(simcom_rpi5_a7670e PRIVATE ${SIMCOM_WARNING_FLAGS})
    target_link_libraries(simcom_rpi5_a7670e PRIVATE simcom_rpi5_app simcom)
endif()
