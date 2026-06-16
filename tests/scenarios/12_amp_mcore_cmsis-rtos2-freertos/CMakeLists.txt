set(FREE_RTOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel")
if(NOT EXISTS "${FREE_RTOS_DIR}/tasks.c")
    if(EXISTS "${FREE_RTOS_DIR}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${FREE_RTOS_DIR}")
    endif()
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
        COMMAND git sparse-checkout set include portable/ThirdParty/GCC/Posix portable/MemMang tasks.c queue.c list.c event_groups.c timers.c
        WORKING_DIRECTORY "${FREE_RTOS_DIR}"
        RESULT_VARIABLE SPARSE_RES
    )
    if(NOT SPARSE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to configure FreeRTOS-Kernel sparse checkout")
    endif()
endif()

set(CMSIS_FREERTOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-FreeRTOS")
if(NOT EXISTS "${CMSIS_FREERTOS_DIR}/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c")
    if(EXISTS "${CMSIS_FREERTOS_DIR}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMSIS_FREERTOS_DIR}")
    endif()
    message(STATUS "Cloning CMSIS-FreeRTOS (sparse, depth 1)...")
    execute_process(
        COMMAND git clone --depth 1 --filter=blob:none --sparse https://github.com/ARM-software/CMSIS-FreeRTOS.git CMSIS-FreeRTOS
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE CLONE_RES
    )
    if(NOT CLONE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to clone CMSIS-FreeRTOS")
    endif()
    execute_process(
        COMMAND git sparse-checkout set CMSIS/RTOS2/FreeRTOS/Source CMSIS/RTOS2/FreeRTOS/Include
        WORKING_DIRECTORY "${CMSIS_FREERTOS_DIR}"
        RESULT_VARIABLE SPARSE_RES
    )
    if(NOT SPARSE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to configure CMSIS-FreeRTOS sparse checkout")
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

# FreeRTOS + CMSIS sources
set(MCORE_SRCS 
    mcore_rtos.c
    FreeRTOS-Kernel/tasks.c
    FreeRTOS-Kernel/queue.c
    FreeRTOS-Kernel/list.c
    FreeRTOS-Kernel/event_groups.c
    FreeRTOS-Kernel/timers.c
    FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/port.c
    FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    FreeRTOS-Kernel/portable/MemMang/heap_3.c
    CMSIS-FreeRTOS/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c
)

add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

add_executable(mcore_rtos ${MCORE_SRCS})
set_target_properties(mcore_rtos PROPERTIES OUTPUT_NAME "mcore_rtos.elf")

# Suppress 64-bit pointer cast and overflow warnings in CMSIS-FreeRTOS / RTOS2 files
target_compile_options(mcore_rtos PRIVATE -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-overflow)

target_include_directories(mcore_rtos PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${FREE_RTOS_DIR}/include"
    "${FREE_RTOS_DIR}/portable/ThirdParty/GCC/Posix"
    "${CMSIS_FREERTOS_DIR}/CMSIS/RTOS2/FreeRTOS/Include"
    "${CMSIS_5_DIR}/CMSIS/RTOS2/Include"
)

find_package(Threads REQUIRED)
target_link_libraries(mcore_rtos PRIVATE Threads::Threads rt)

add_custom_command(TARGET mcore_rtos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_rtos> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_rtos.elf"
)
