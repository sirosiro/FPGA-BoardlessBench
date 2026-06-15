add_executable(test_bin main.cpp hal/imx_hal.cpp)
target_include_directories(test_bin PRIVATE 
    "${PROJECT_SOURCE_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
find_package(Threads REQUIRED)
target_link_libraries(test_bin PRIVATE 
    Threads::Threads
    EGL
    GLESv2
)
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)
