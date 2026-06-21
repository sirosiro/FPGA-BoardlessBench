# Set position independent code so the static libraries can be linked into fpgashim.so
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

cmake_minimum_required(VERSION 3.12)
project(15_amp_mcore_OpenAMP_freertos C)
cmake_policy(SET CMP0079 NEW)

# Set external checkout directory policies
include(FetchContent)

# 1. libmetal setup
set(MACHINE "template" CACHE STRING "" FORCE)
set(WITH_SYSFS OFF CACHE BOOL "" FORCE)
set(WITH_HUGETLBFS OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_DEFAULT_LOGGER ON CACHE BOOL "" FORCE)

# Save system name and force Generic for libmetal/open-amp config
set(ORIGINAL_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
set(CMAKE_SYSTEM_NAME "Generic" CACHE STRING "" FORCE)

FetchContent_Declare(
    libmetal
    GIT_REPOSITORY https://github.com/OpenAMP/libmetal.git
    GIT_TAG        v2023.04.0
)
FetchContent_MakeAvailable(libmetal)

# libmetal header paths (required by open-amp build process)
set(LIBMETAL_INC_DIRS
    "${CMAKE_BINARY_DIR}/_deps/libmetal-src/lib/include"
    "${CMAKE_BINARY_DIR}/_deps/libmetal-build/lib/include"
)
include_directories(${LIBMETAL_INC_DIRS})

# 2. open-amp setup
set(WITH_LIBMETAL_FIND OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_ZEPHYR OFF CACHE BOOL "" FORCE)
set(WITH_PROXY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    openamp
    GIT_REPOSITORY https://github.com/OpenAMP/open-amp.git
    GIT_TAG        v2023.04.0
)
FetchContent_MakeAvailable(openamp)

# Restore system name for application targets
set(CMAKE_SYSTEM_NAME ${ORIGINAL_SYSTEM_NAME} CACHE STRING "" FORCE)

# open-amp header paths
set(OPENAMP_INC_DIRS
    "${CMAKE_BINARY_DIR}/_deps/openamp-src/lib/include"
)

# Configure fpgashim with OpenAMP support
target_compile_definitions(fpgashim PRIVATE FBB_WITH_OPENAMP)
target_include_directories(fpgashim PRIVATE
    ${LIBMETAL_INC_DIRS}
    ${OPENAMP_INC_DIRS}
)
target_link_libraries(fpgashim open_amp-static metal-static Threads::Threads)

# A-Core Test Binary
add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

# 3. FreeRTOS setup
set(FREE_RTOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel")
if(NOT EXISTS "${FREE_RTOS_DIR}/tasks.c")
    message(STATUS "Cloning FreeRTOS-Kernel (sparse, depth 1)...")
    execute_process(
        COMMAND git clone --depth 1 --filter=blob:none --sparse https://github.com/FreeRTOS/FreeRTOS-Kernel.git FreeRTOS-Kernel
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE CLONE_RES
    )
    if(NOT CLONE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to clone FreeRTOS-Kernel")
    endif()
    execute_process(
        COMMAND git sparse-checkout set include portable/ThirdParty/GCC/Posix portable/MemMang tasks.c queue.c list.c
        WORKING_DIRECTORY "${FREE_RTOS_DIR}"
        RESULT_VARIABLE SPARSE_RES
    )
    if(NOT SPARSE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to configure FreeRTOS-Kernel sparse checkout")
    endif()
endif()

# FreeRTOS sources
set(MCORE_SRCS 
    mcore_rtos.c
    FreeRTOS-Kernel/tasks.c
    FreeRTOS-Kernel/queue.c
    FreeRTOS-Kernel/list.c
    FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/port.c
    FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    FreeRTOS-Kernel/portable/MemMang/heap_3.c
)

# M-Core FreeRTOS Firmware
add_executable(mcore_rtos ${MCORE_SRCS})
set_target_properties(mcore_rtos PROPERTIES OUTPUT_NAME "mcore_rtos.elf")
target_include_directories(mcore_rtos PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${FREE_RTOS_DIR}/include"
    "${FREE_RTOS_DIR}/portable/ThirdParty/GCC/Posix"
    ${LIBMETAL_INC_DIRS}
    ${OPENAMP_INC_DIRS}
)
find_package(Threads REQUIRED)
target_link_libraries(mcore_rtos PRIVATE Threads::Threads rt open_amp-static metal-static)
add_custom_command(TARGET mcore_rtos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_rtos> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_rtos.elf"
)
