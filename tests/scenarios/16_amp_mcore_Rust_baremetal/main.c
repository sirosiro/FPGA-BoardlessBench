#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "vfpga_device_config.h"
#define UIO_DEVICE FBB_DEV_PATH_VFPGA_REG
#define REG_SIZE 1024

// Register indexes for regs array (uint32_t indices)
// 0x10 -> index 4 (CMD)
// 0x14 -> index 5 (STATUS)
// 0x18 -> index 6 (DATA_IN)
// 0x1c -> index 7 (DATA_OUT)
#define IDX_CMD      4
#define IDX_STATUS   5
#define IDX_DATA_IN  6
#define IDX_DATA_OUT 7

int main() {
    printf("--- Rust AMP Baremetal Test Start ---\n");

    // Open UIO and map shared memory
    printf("[A-Core] Opening %s...\n", UIO_DEVICE);
    int uio_fd = open(UIO_DEVICE, O_RDWR);
    if (uio_fd == -1) {
        perror("open failed");
        return 1;
    }

    volatile uint32_t *regs = mmap(NULL, REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);
    if (regs == MAP_FAILED) {
        perror("mmap failed");
        close(uio_fd);
        return 1;
    }

    // Initialize/Clear registers before test
    regs[IDX_CMD] = 0;
    regs[IDX_STATUS] = 0;
    regs[IDX_DATA_IN] = 0;
    regs[IDX_DATA_OUT] = 0;

    // Wait briefly for M-Core tasks to settle
    sleep(1);

    // Test data
    uint32_t test_val = 12345;
    uint32_t expected_val = test_val * 2;

    printf("[A-Core] Writing test value %u to REG_DATA_IN...\n", test_val);
    regs[IDX_DATA_IN] = test_val;

    printf("[A-Core] Sending Command 0xA1 to REG_CMD...\n");
    regs[IDX_CMD] = 0xA1;

    // Poll STATUS register for processing completion (0x01 = READY)
    printf("[A-Core] Waiting for M-Core to set REG_STATUS to READY (0x01)...\n");
    int timeout = 50; // 50 * 100ms = 5s
    while (regs[IDX_STATUS] != 0x01 && timeout > 0) {
        usleep(100000); // 100ms
        timeout--;
    }

    if (regs[IDX_STATUS] == 0x01) {
        printf("[A-Core] READY status received from M-Core.\n");
        uint32_t result = regs[IDX_DATA_OUT];
        printf("[A-Core] Reading REG_DATA_OUT: %u (Expected: %u)\n", result, expected_val);
        
        if (result == expected_val) {
            printf("[A-Core] SUCCESS: Data correctly processed by Rust baremetal firmware!\n");
            munmap((void*)regs, REG_SIZE);
            close(uio_fd);
            printf("--- Rust AMP Baremetal Test Finished ---\n");
            return 0;
        } else {
            printf("[A-Core] ERROR: Value mismatch! Received %u but expected %u.\n", result, expected_val);
        }
    } else {
        printf("[A-Core] ERROR: Timeout waiting for M-Core processing to complete (REG_STATUS is still %u).\n", regs[IDX_STATUS]);
    }

    munmap((void*)regs, REG_SIZE);
    close(uio_fd);
    printf("--- Rust AMP Baremetal Test Failed ---\n");
    return 1;
}
