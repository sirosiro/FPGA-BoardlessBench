set(SFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/simple-functions-scheduler")
if(NOT EXISTS "${SFS_DIR}/sfs.c")
    message(STATUS "Cloning simple-functions-scheduler (latest master, depth 1)...")
    execute_process(
        COMMAND git clone --depth 1 https://github.com/sirosiro/simple-functions-scheduler.git simple-functions-scheduler
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE CLONE_RES
    )
    if(NOT CLONE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to clone simple-functions-scheduler")
    endif()
endif()

add_executable(test_bin main.c)
target_include_directories(test_bin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/include"
)
add_custom_command(TARGET test_bin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_bin> "${CMAKE_CURRENT_SOURCE_DIR}/"
)

set(MCORE_SFS_SRCS
    mcore_sfs.c
    bsp_sim_irq.c
    "${SFS_DIR}/sfs.c"
    "${SFS_DIR}/libs/frcc/frcc.c"
    "${SFS_DIR}/libs/fifo/fifo.c"
    "${SFS_DIR}/libs/matrix/state_machine.c"
    "${SFS_DIR}/libs/ring_buffer/ring_buffer.c"
)

add_executable(mcore_sfs ${MCORE_SFS_SRCS})
set_target_properties(mcore_sfs PROPERTIES OUTPUT_NAME "mcore_sfs.elf")
target_include_directories(mcore_sfs PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SFS_DIR}"
    "${SFS_DIR}/libs/frcc"
    "${SFS_DIR}/libs/fifo"
    "${SFS_DIR}/libs/matrix"
    "${SFS_DIR}/libs/ring_buffer"
)
target_compile_options(mcore_sfs PRIVATE -Wno-write-strings)
add_custom_command(TARGET mcore_sfs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcore_sfs> "${CMAKE_CURRENT_SOURCE_DIR}/mcore_sfs.elf"
)
