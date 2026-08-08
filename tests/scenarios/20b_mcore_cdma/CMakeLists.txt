# Set position independent code so the static libraries can be linked into fpgashim.so
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

cmake_minimum_required(VERSION 3.12)
project(20b_mcore_cdma C)
cmake_policy(SET CMP0079 NEW)

# A-Core Test Binary
add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

# M-Core Baremetal Firmware
add_executable(mcore_cdma mcore_cdma.c)
set_target_properties(mcore_cdma PROPERTIES OUTPUT_NAME "mcore_cdma.elf")
target_include_directories(mcore_cdma PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET mcore_cdma POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_cdma> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_cdma.elf"
)
