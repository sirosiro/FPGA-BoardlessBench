#include "tx_api.h"
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define REG_CMD      (*(volatile uint32_t*)(uintptr_t)(0x40000010))
#define REG_STATUS   (*(volatile uint32_t*)(uintptr_t)(0x40000014))
#define REG_DATA_IN  (*(volatile uint32_t*)(uintptr_t)(0x40000018))
#define REG_DATA_OUT (*(volatile uint32_t*)(uintptr_t)(0x4000001c))

#define POOL_SIZE 65536
static uint8_t threadx_pool[POOL_SIZE];
static TX_BYTE_POOL byte_pool;

static TX_THREAD thread_controller;
static TX_THREAD thread_processor;
static TX_QUEUE queue_msg;

static void thread_controller_entry(ULONG thread_input);
static void thread_processor_entry(ULONG thread_input);

int main(void) {
    printf("[M-Core] ThreadX firmware starting...\n");
    fflush(stdout);

    /* Enter the ThreadX kernel. */
    tx_kernel_enter();
    return 0;
}

void tx_application_define(void *first_unused_memory) {
    (void)first_unused_memory;
    void *stack_ptr;
    void *queue_ptr;

    /* Create the byte memory pool. */
    tx_byte_pool_create(&byte_pool, "byte pool 0", threadx_pool, POOL_SIZE);

    /* Allocate stack and create Controller thread. */
    tx_byte_allocate(&byte_pool, &stack_ptr, 4096, TX_NO_WAIT);
    tx_thread_create(&thread_controller, "FPGA_Ctrl", thread_controller_entry, 0,
                     stack_ptr, 4096, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);

    /* Allocate stack and create Processor thread. */
    tx_byte_allocate(&byte_pool, &stack_ptr, 4096, TX_NO_WAIT);
    tx_thread_create(&thread_processor, "Data_Proc", thread_processor_entry, 0,
                     stack_ptr, 4096, 2, 2, TX_NO_TIME_SLICE, TX_AUTO_START);

    /* Allocate queue area and create message queue. */
    tx_byte_allocate(&byte_pool, &queue_ptr, sizeof(uint32_t) * 10, TX_NO_WAIT);
    tx_queue_create(&queue_msg, "queue 0", TX_1_ULONG, queue_ptr, sizeof(uint32_t) * 10);
}

static void thread_controller_entry(ULONG thread_input) {
    (void)thread_input;
    printf("[M-Core] FPGA Controller thread started.\n");
    fflush(stdout);

    while (1) {
        uint32_t cmd = REG_CMD;
        if (cmd == 0xA1) {
            uint32_t data = REG_DATA_IN;
            printf("[M-Core] Command 0xA1 detected. Data input: %u. Sending to processing queue...\n", data);
            fflush(stdout);

            if (tx_queue_send(&queue_msg, &data, TX_NO_WAIT) != TX_SUCCESS) {
                printf("[M-Core] Error: Failed to send data to queue.\n");
                fflush(stdout);
            }

            REG_CMD = 0; // Clear CMD to acknowledge
        }
        tx_thread_sleep(1); // 10ms (1 tick = 10ms)
    }
}

static void thread_processor_entry(ULONG thread_input) {
    (void)thread_input;
    uint32_t input_val = 0;

    printf("[M-Core] Data Processor thread started.\n");
    fflush(stdout);

    while (1) {
        if (tx_queue_receive(&queue_msg, &input_val, TX_WAIT_FOREVER) == TX_SUCCESS) {
            printf("[M-Core] Processing data: %u...\n", input_val);
            fflush(stdout);

            tx_thread_sleep(10); // 100ms (10 ticks = 100ms)

            uint32_t result = input_val * 2;
            REG_DATA_OUT = result;
            REG_STATUS = 0x01; // Set READY status

            printf("[M-Core] Data processed. Result %u written to REG_DATA_OUT.\n", result);
            fflush(stdout);
        }
    }
}
