cmake_minimum_required(VERSION 3.16)
project(scenario_06b_hub75_matrix_64x64 CXX C)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../src/include)

add_executable(test_bin main.cpp)
target_link_libraries(test_bin PRIVATE pthread)
