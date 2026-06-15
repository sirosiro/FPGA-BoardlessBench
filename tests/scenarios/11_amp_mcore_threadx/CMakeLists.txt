set(THREADX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/threadx")
if(NOT EXISTS "${THREADX_DIR}/common/src/tx_thread_create.c")
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
endif()

# Collect ThreadX common and port sources
file(GLOB THREADX_COMMON_SRCS "${THREADX_DIR}/common/src/*.c")
file(GLOB THREADX_PORT_SRCS "${THREADX_DIR}/ports/linux/gnu/src/*.c")
set(THREADX_SRCS ${THREADX_COMMON_SRCS} ${THREADX_PORT_SRCS})

# M-Core ThreadX firmware executable sources
set(MCORE_SRCS 
    mcore_threadx.c
    ${THREADX_SRCS}
)

add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE "${PROJECT_SOURCE_DIR}/src/include")
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

add_executable(mcore_threadx ${MCORE_SRCS})
set_target_properties(mcore_threadx PROPERTIES OUTPUT_NAME "mcore_threadx.elf")
target_compile_definitions(mcore_threadx PRIVATE _GNU_SOURCE TX_SINGLE_CORE)
target_include_directories(mcore_threadx PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${THREADX_DIR}/common/inc"
    "${THREADX_DIR}/ports/linux/gnu/inc"
)
find_package(Threads REQUIRED)
target_link_libraries(mcore_threadx PRIVATE Threads::Threads rt)
add_custom_command(TARGET mcore_threadx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_threadx> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_threadx.elf"
)
