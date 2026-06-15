add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

add_executable(mcore_baremetal mcore_baremetal.c)
target_include_directories(mcore_baremetal PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
set_target_properties(mcore_baremetal PROPERTIES OUTPUT_NAME "mcore_baremetal.elf")
add_custom_command(TARGET mcore_baremetal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_baremetal> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_baremetal.elf"
)

add_executable(mcore_baremetal2 mcore_baremetal2.c)
target_include_directories(mcore_baremetal2 PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
set_target_properties(mcore_baremetal2 PROPERTIES OUTPUT_NAME "mcore_baremetal2.elf")
add_custom_command(TARGET mcore_baremetal2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_baremetal2> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_baremetal2.elf"
)
