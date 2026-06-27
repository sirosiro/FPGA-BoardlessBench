#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#define UIO_DEVICE "/dev/uio0"
#define REG_SIZE 1024

// Register indexes for regs array (uint32_t indices)
#define IDX_CMD      4
#define IDX_STATUS   5
#define IDX_DATA_IN  6
#define IDX_DATA_OUT 7

// Helper to trigger virtual IPI (SIGUSR1) to M-Core
void trigger_mcore_interrupt() {
    FILE *fp = fopen("/tmp/fbb/sys/class/remoteproc/remoteproc0/pid", "r");
    if (!fp) {
        perror("[A-Core] Failed to open M-Core pid file");
        return;
    }
    int pid = 0;
    if (fscanf(fp, "%d", &pid) == 1 && pid > 0) {
        printf("[A-Core] Sending SIGUSR1 to M-Core PID %d...\n", pid);
        kill(pid, SIGUSR1);
    } else {
        printf("[A-Core] Error reading M-Core PID.\n");
    }
    fclose(fp);
}

int main() {
    printf("--- Rust AMP RTIC Test Start ---\n");

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
    uint32_t expected_val = test_val * 4; // RTIC scenario uses multiplier 4

    printf("[A-Core] Writing test value %u to REG_DATA_IN...\n", test_val);
    regs[IDX_DATA_IN] = test_val;

    printf("[A-Core] Sending Command 0xA1 to REG_CMD...\n");
    regs[IDX_CMD] = 0xA1;

    // Trigger virtual IPI interrupt (SIGUSR1) to M-Core
    printf("[A-Core] Triggering Virtual Interrupt (SIGUSR1) to M-Core...\n");
    trigger_mcore_interrupt();

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
            printf("[A-Core] SUCCESS: Data correctly processed by Rust RTIC interrupt handler!\n");
            munmap((void*)regs, REG_SIZE);
            close(uio_fd);
            printf("--- Rust AMP RTIC Test Finished ---\n");
            return 0;
        } else {
            printf("[A-Core] ERROR: Value mismatch! Received %u but expected %u.\n", result, expected_val);
        }
    } else {
        printf("[A-Core] ERROR: Timeout waiting for M-Core processing to complete (REG_STATUS is still %u).\n", regs[IDX_STATUS]);
    }

    munmap((void*)regs, REG_SIZE);
    close(uio_fd);
    printf("--- Rust AMP RTIC Test Failed ---\n");
    return 1;
}
