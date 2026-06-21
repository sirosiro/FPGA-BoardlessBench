# Set position independent code so the static libraries can be linked into fpgashim.so
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

cmake_minimum_required(VERSION 3.12)
project(14_amp_mcore_OpenAMP_baremetal C)
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

# M-Core Baremetal Firmware
add_executable(mcore_baremetal mcore_baremetal.c)
set_target_properties(mcore_baremetal PROPERTIES OUTPUT_NAME "mcore_baremetal.elf")
target_include_directories(mcore_baremetal PRIVATE 
    "${PROJECT_SOURCE_DIR}/src/include"
    ${LIBMETAL_INC_DIRS}
    ${OPENAMP_INC_DIRS}
)
target_link_libraries(mcore_baremetal PRIVATE open_amp-static metal-static)
add_custom_command(TARGET mcore_baremetal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_baremetal> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_baremetal.elf"
)
