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

add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

add_executable(mcore_rtos ${MCORE_SRCS})
set_target_properties(mcore_rtos PROPERTIES OUTPUT_NAME "mcore_rtos.elf")
target_include_directories(mcore_rtos PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${FREE_RTOS_DIR}/include"
    "${FREE_RTOS_DIR}/portable/ThirdParty/GCC/Posix"
)
find_package(Threads REQUIRED)
target_link_libraries(mcore_rtos PRIVATE Threads::Threads rt)
add_custom_command(TARGET mcore_rtos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_rtos> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_rtos.elf"
)
