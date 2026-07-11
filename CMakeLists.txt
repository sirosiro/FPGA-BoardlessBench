cmake_minimum_required(VERSION 3.12)
project(FPGA-BoardlessBench C CXX)
cmake_policy(SET CMP0079 NEW)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile_commands.json export
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Active test scenario directory
set(SCENARIO_DIR "" CACHE PATH "Path to the active test scenario")

# 1. Build the Interception Shim (libfpgashim.so)
add_library(fpgashim SHARED src/shim/libfpgashim.c)
target_include_directories(fpgashim PRIVATE src/include)
set_target_properties(fpgashim PROPERTIES 
    PREFIX "" 
    OUTPUT_NAME "libfpgashim"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(fpgashim ${CMAKE_DL_LIBS})

# Post-build: copy libfpgashim.so to PROJECT_SOURCE_DIR for compatibility
add_custom_command(TARGET fpgashim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fpgashim> "${PROJECT_SOURCE_DIR}/"
)

# 2. Build the Simulator (vfpga_sim)
if(NOT SCENARIO_DIR STREQUAL "")
    get_filename_component(SCENARIO_NAME "${SCENARIO_DIR}" NAME)
    
    # Check if the scenario provides its own vfpga_top.v
    if(EXISTS "${SCENARIO_DIR}/vfpga_top.v")
        get_filename_component(RTL_TOP "${SCENARIO_DIR}/vfpga_top.v" ABSOLUTE)
    else()
        get_filename_component(RTL_TOP "${PROJECT_SOURCE_DIR}/src/rtl/vfpga_top.v" ABSOLUTE)
    endif()
    
    # Collect all scenario-specific Verilog files (excluding vfpga_top.v if already picked)
    file(GLOB SCENARIO_V_FILES "${SCENARIO_DIR}/*.v")
    if(SCENARIO_V_FILES)
        list(REMOVE_ITEM SCENARIO_V_FILES "${RTL_TOP}")
    endif()
    
    set(RTL_SRCS ${RTL_TOP} ${SCENARIO_V_FILES})
    
    # Verilator execution
    set(VERILATOR_OUTPUT_DIR "${CMAKE_BINARY_DIR}/obj_dir")
    
    add_custom_command(
        OUTPUT 
            "${VERILATOR_OUTPUT_DIR}/Vvfpga_top__ALL.a"
            "${VERILATOR_OUTPUT_DIR}/Vvfpga_top.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VERILATOR_OUTPUT_DIR}"
        COMMAND verilator -Wall --cc --trace -CFLAGS "-I${PROJECT_SOURCE_DIR}/src/include -std=c++17" --Mdir "${VERILATOR_OUTPUT_DIR}" ${RTL_SRCS}
        COMMAND make -C "${VERILATOR_OUTPUT_DIR}" -f Vvfpga_top.mk
        DEPENDS ${RTL_SRCS}
        COMMENT "Running Verilator to generate simulation model"
    )
    
    add_custom_target(generate_verilator DEPENDS "${VERILATOR_OUTPUT_DIR}/Vvfpga_top__ALL.a")
    
    # Find verilator root directory
    if(NOT DEFINED VERILATOR_ROOT)
        execute_process(
            COMMAND verilator --getenv VERILATOR_ROOT
            OUTPUT_VARIABLE VERILATOR_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(VERILATOR_ROOT STREQUAL "")
            set(VERILATOR_ROOT "/usr/share/verilator")
        endif()
    endif()
    
    set(SIM_SRCS
        src/sim/sim_main.cpp
        ${VERILATOR_ROOT}/include/verilated.cpp
        ${VERILATOR_ROOT}/include/verilated_vcd_c.cpp
    )
    
    add_executable(vfpga_sim ${SIM_SRCS})
    add_dependencies(vfpga_sim generate_verilator)
    
    target_include_directories(vfpga_sim PRIVATE
        src/include
        ${VERILATOR_ROOT}/include
        ${VERILATOR_ROOT}/include/vltstd
        ${VERILATOR_OUTPUT_DIR}
    )
    find_package(Threads REQUIRED)
    target_link_libraries(vfpga_sim PRIVATE 
        Threads::Threads
        "${VERILATOR_OUTPUT_DIR}/Vvfpga_top__ALL.a"
    )
    
    set_target_properties(vfpga_sim PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    
    # Post-build: copy vfpga_sim to PROJECT_SOURCE_DIR for compatibility
    add_custom_command(TARGET vfpga_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:vfpga_sim> "${PROJECT_SOURCE_DIR}/"
    )

    # 3. Add active scenario subdirectory if CMakeLists.txt exists
    if(EXISTS "${SCENARIO_DIR}/CMakeLists.txt")
        add_subdirectory("${SCENARIO_DIR}" "${CMAKE_BINARY_DIR}/scenarios/${SCENARIO_NAME}")
        

    endif()

    # 4. Add shared peripherals library
    add_subdirectory(src/peripherals)
endif()

# Copy compile_commands.json to source root for VS Code/LSP Intellisense
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${PROJECT_SOURCE_DIR}/compile_commands.json"
        COMMENT "Copying compile_commands.json to source root for Intellisense"
    )
endif()
