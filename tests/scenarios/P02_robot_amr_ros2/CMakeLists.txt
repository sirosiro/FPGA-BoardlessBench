cmake_minimum_required(VERSION 3.16)
project(fbb_scenario_P02_robot_amr_ros2 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# On-demand ROS 2 hardware_interface detection
find_package(hardware_interface QUIET)

add_executable(test_bin
    main.cpp
    uio_robot_hardware.cpp
    amr_kinematics.cpp
)

target_include_directories(test_bin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src/include"
)

if(hardware_interface_FOUND)
    message(STATUS "[Scenario P02] Found system ROS 2 hardware_interface. Linking with ROS 2...")
    target_link_libraries(test_bin PRIVATE hardware_interface::hardware_interface)
else()
    message(STATUS "[Scenario P02] Using zero-install embedded hardware_interface compatibility layer.")
endif()

find_package(Threads REQUIRED)
target_link_libraries(test_bin PRIVATE Threads::Threads rt)

add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/test_bin"
)
