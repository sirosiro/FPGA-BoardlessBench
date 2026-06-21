set(THREADX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/threadx")
if(NOT EXISTS "${THREADX_DIR}/common/src/tx_thread_create.c")
    if(EXISTS "${THREADX_DIR}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${THREADX_DIR}")
    endif()
    message(STATUS "Cloning ThreadX (sparse, depth 1)...")
    execute_process(
        COMMAND git clone --depth 1 --branch v6.4.0_rel --filter=blob:none --sparse https://github.com/eclipse-threadx/threadx.git threadx
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE CLONE_RES
    )
    if(NOT CLONE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to clone ThreadX")
    endif()
    execute_process(
        COMMAND git sparse-checkout set common ports/linux/gnu
        WORKING_DIRECTORY "${THREADX_DIR}"
        RESULT_VARIABLE SPARSE_RES
    )
    if(NOT SPARSE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to configure ThreadX sparse checkout")
    endif()

    # Comment out unconditional TX_MAX_PRIORITIES redefinition in tx_port.h to avoid compiler warnings and allow override
    execute_process(
        COMMAND sed -i "s/#define TX_MAX_PRIORITIES                       32/\\/\\* #define TX_MAX_PRIORITIES                       32 \\*\\//g" ports/linux/gnu/inc/tx_port.h
        WORKING_DIRECTORY "${THREADX_DIR}"
    )
endif()

set(CMSIS_THREADX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stm32-mw-cmsis-rtos-tx")
if(NOT EXISTS "${CMSIS_THREADX_DIR}/cmsis_os2.c")
    if(EXISTS "${CMSIS_THREADX_DIR}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMSIS_THREADX_DIR}")
    endif()
    message(STATUS "Cloning stm32-mw-cmsis-rtos-tx (sparse, depth 1)...")
    execute_process(
        COMMAND git clone --depth 1 --filter=blob:none --sparse https://github.com/STMicroelectronics/stm32-mw-cmsis-rtos-tx.git stm32-mw-cmsis-rtos-tx
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE CLONE_RES
    )
    if(NOT CLONE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to clone stm32-mw-cmsis-rtos-tx")
    endif()
    execute_process(
        COMMAND git sparse-checkout set Include cmsis_os2.c
        WORKING_DIRECTORY "${CMSIS_THREADX_DIR}"
        RESULT_VARIABLE SPARSE_RES
    )
    if(NOT SPARSE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to configure stm32-mw-cmsis-rtos-tx sparse checkout")
    endif()
endif()

set(CMSIS_5_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5")
if(NOT EXISTS "${CMSIS_5_DIR}/CMSIS/RTOS2/Include/cmsis_os2.h")
    if(EXISTS "${CMSIS_5_DIR}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMSIS_5_DIR}")
    endif()
    message(STATUS "Cloning CMSIS_5 (sparse, depth 1)...")
    execute_process(
        COMMAND git clone --depth 1 --filter=blob:none --sparse https://github.com/ARM-software/CMSIS_5.git CMSIS_5
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE CLONE_RES
    )
    if(NOT CLONE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to clone CMSIS_5")
    endif()
    execute_process(
        COMMAND git sparse-checkout set CMSIS/RTOS2/Include
        WORKING_DIRECTORY "${CMSIS_5_DIR}"
        RESULT_VARIABLE SPARSE_RES
    )
    if(NOT SPARSE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to configure CMSIS_5 sparse checkout")
    endif()
endif()

# Collect ThreadX common and port sources
file(GLOB THREADX_COMMON_SRCS "${THREADX_DIR}/common/src/*.c")
file(GLOB THREADX_PORT_SRCS "${THREADX_DIR}/ports/linux/gnu/src/*.c")
set(THREADX_SRCS ${THREADX_COMMON_SRCS} ${THREADX_PORT_SRCS})

# Timestamp-based synchronization for mcore_rtos.c between Scenario 12 and 13
set(FILE_12 "${CMAKE_CURRENT_SOURCE_DIR}/../12_amp_mcore_cmsis-rtos2-freertos/mcore_rtos.c")
set(FILE_13 "${CMAKE_CURRENT_SOURCE_DIR}/../13_amp_mcore_cmsis-rtos2-threadx/mcore_rtos.c")

if(EXISTS "${FILE_12}" AND EXISTS "${FILE_13}")
    # Check if contents are different first to avoid timestamp feedback loop
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files "${FILE_12}" "${FILE_13}"
        RESULT_VARIABLE FILES_DIFF
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(FILES_DIFF)
        file(TIMESTAMP "${FILE_12}" TIME_12 "%Y%m%d%H%M%S")
        file(TIMESTAMP "${FILE_13}" TIME_13 "%Y%m%d%H%M%S")
        if(TIME_12 STRGREATER TIME_13)
            message(STATUS "Syncing: Scenario 12 mcore_rtos.c is newer (${TIME_12} > ${TIME_13}). Copying to Scenario 13...")
            configure_file("${FILE_12}" "${FILE_13}" COPYONLY)
        else()
            message(STATUS "Syncing: Scenario 13 mcore_rtos.c is newer (${TIME_13} > ${TIME_12}). Copying to Scenario 12...")
            configure_file("${FILE_13}" "${FILE_12}" COPYONLY)
        endif()
    endif()
elseif(EXISTS "${FILE_12}" AND NOT EXISTS "${FILE_13}")
    get_filename_component(DIR_13 "${FILE_13}" DIRECTORY)
    if(IS_DIRECTORY "${DIR_13}")
        message(STATUS "Scenario 13 file is missing. Copying from Scenario 12...")
        configure_file("${FILE_12}" "${FILE_13}" COPYONLY)
    endif()
elseif(EXISTS "${FILE_13}" AND NOT EXISTS "${FILE_12}")
    get_filename_component(DIR_12 "${FILE_12}" DIRECTORY)
    if(IS_DIRECTORY "${DIR_12}")
        message(STATUS "Scenario 12 file is missing. Copying from Scenario 13...")
        configure_file("${FILE_13}" "${FILE_12}" COPYONLY)
    endif()
endif()

# M-Core ThreadX firmware executable sources
set(MCORE_SRCS 
    mcore_rtos.c
    ${THREADX_SRCS}
    stm32-mw-cmsis-rtos-tx/cmsis_os2.c
)

add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

add_executable(mcore_rtos ${MCORE_SRCS})
set_target_properties(mcore_rtos PROPERTIES OUTPUT_NAME "mcore_rtos.elf")

# Suppress 64-bit pointer cast and overflow warnings in ThreadX / RTOS2 files
target_compile_options(mcore_rtos PRIVATE -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-overflow)

# Macro definitions for ThreadX POSIX port
target_compile_definitions(mcore_rtos PRIVATE 
    _GNU_SOURCE 
    TX_SINGLE_CORE
    CMSIS_device_header="cmsis_compiler.h"
    TX_MAX_PRIORITIES=64
    USE_DYNAMIC_MEMORY_ALLOCATION
    "TX_THREAD_USER_EXTENSION=ULONG tx_thread_detached_joinable\;"
    TX_LINUX_MEMORY_SIZE=1048576
    RTOS2_BYTE_POOL_STACK_SIZE=262144
    RTOS2_BYTE_POOL_HEAP_SIZE=262144
)

target_include_directories(mcore_rtos PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${THREADX_DIR}/common/inc"
    "${THREADX_DIR}/ports/linux/gnu/inc"
    "${CMSIS_THREADX_DIR}/Include"
    "${CMSIS_5_DIR}/CMSIS/RTOS2/Include"
)

find_package(Threads REQUIRED)
target_link_libraries(mcore_rtos PRIVATE Threads::Threads rt)

add_custom_command(TARGET mcore_rtos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_rtos> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_rtos.elf"
)
