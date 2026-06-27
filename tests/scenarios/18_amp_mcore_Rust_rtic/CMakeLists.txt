cmake_minimum_required(VERSION 3.12)
project(test_bin C)

set(CMAKE_C_STANDARD 99)

add_executable(test_bin a_core/main.c)
target_include_directories(test_bin PRIVATE ${PROJECT_SOURCE_DIR}/src/include)

set_target_properties(test_bin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
