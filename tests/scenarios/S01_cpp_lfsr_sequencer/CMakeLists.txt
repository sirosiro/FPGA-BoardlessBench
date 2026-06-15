file(GLOB SRCS *.cpp)
add_executable(test_bin ${SRCS})
target_compile_options(test_bin PRIVATE -O2)
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)
