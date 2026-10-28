add_library(unity STATIC
    unity/unity.c
    mocks/mock_simcom_hal.c
)

target_include_directories(unity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/unity
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/inc/internal
)

function(simcom_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE simcom unity)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/inc
        ${CMAKE_SOURCE_DIR}/inc/internal
        ${CMAKE_CURRENT_SOURCE_DIR}/unity
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

simcom_add_test(test_ringbuf unit/test_ringbuf.c)
simcom_add_test(test_parser unit/test_parser.c)
simcom_add_test(test_lexer_urc unit/test_lexer_urc.c)
simcom_add_test(test_init_recovery unit/test_init_recovery.c)
simcom_add_test(test_sim_device_info unit/test_sim_device_info.c)
simcom_add_test(test_network_status unit/test_network_status.c)
simcom_add_test(test_logging_config unit/test_logging_config.c)
