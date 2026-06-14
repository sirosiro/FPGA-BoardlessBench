#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include "vfpga_config.h"

#define UIO_DEVICE "/dev/uio0"
#define REG_SIZE 1024

static int write_state(const char *state) {
    int fd = open("/sys/class/remoteproc/remoteproc0/state", O_WRONLY | O_TRUNC);
    if (fd == -1) {
        perror("Failed to open remoteproc state path for writing");
        return -1;
    }
    if (write(fd, state, strlen(state)) == -1) {
        perror("Failed to write to state");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int main() {
    printf("--- remoteproc AMP Test Start ---\n");

    // mcore_baremetal.elf (FW1) has already been copied, loaded, and started by run.sh.
    // We can directly verify its status and interact with it.

    // 1. Verify that remoteproc0 is running
    char state_buf[64];
    int state_fd = open("/sys/class/remoteproc/remoteproc0/state", O_RDWR);
    if (state_fd == -1) {
        perror("Failed to open remoteproc state path");
        return 1;
    }
    ssize_t n = read(state_fd, state_buf, sizeof(state_buf) - 1);
    if (n >= 0) {
        state_buf[n] = '\0';
        // Remove trailing whitespace/newlines
        while (n > 0 && (state_buf[n-1] == '\n' || state_buf[n-1] == '\r' || state_buf[n-1] == ' ')) {
            state_buf[n-1] = '\0';
            n--;
        }
    } else {
        state_buf[0] = '\0';
    }
    printf("[A-Core] Initial remoteproc state: %s\n", state_buf);

    // 2. Open UIO and map shared memory
    printf("[A-Core] Opening %s...\n", UIO_DEVICE);
    int uio_fd = open(UIO_DEVICE, O_RDWR);
    if (uio_fd == -1) {
        perror("open failed");
        close(state_fd);
        return 1;
    }

    volatile uint32_t *regs = mmap(NULL, REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);
    if (regs == MAP_FAILED) {
        perror("mmap failed");
        close(uio_fd);
        close(state_fd);
        return 1;
    }

    // 3. Reset and Enable counter for FW1
    printf("[A-Core] Resetting counter for FW1...\n");
    regs[4] = 1; // RST @ 0x10 -> index 4
    regs[4] = 0;

    printf("[A-Core] Enabling counter for FW1...\n");
    regs[5] = 1; // EN @ 0x14 -> index 5

    // Wait for counter to increment (and M-Core to monitor it)
    printf("[A-Core] Waiting for FW1 to monitor counter...\n");
    sleep(2);

    uint32_t val1 = regs[6]; // CNT @ 0x18 -> index 6
    printf("[A-Core] Counter value with FW1: %u\n", val1);
    int fw1_success = (val1 > 0);

    // 4. Stop FW1
    printf("[A-Core] Stopping FW1...\n");
    write_state("stop");

    // Wait until remoteproc state becomes offline
    printf("[A-Core] Waiting for remoteproc to become offline...\n");
    while (1) {
        lseek(state_fd, 0, SEEK_SET);
        ssize_t temp_n = read(state_fd, state_buf, sizeof(state_buf) - 1);
        if (temp_n >= 0) {
            state_buf[temp_n] = '\0';
            ssize_t temp_len = temp_n;
            while (temp_len > 0 && (state_buf[temp_len-1] == '\n' || state_buf[temp_len-1] == '\r' || state_buf[temp_len-1] == ' ')) {
                state_buf[temp_len-1] = '\0';
                temp_len--;
            }
        } else {
            state_buf[0] = '\0';
        }
        if (strcmp(state_buf, "offline") == 0) {
            break;
        }
        usleep(50000); // 50ms
    }
    printf("[A-Core] remoteproc state is offline. FW1 stopped successfully!\n");

    // 5. Hot-swap to FW2 (mcore_baremetal2.elf)
    printf("[A-Core] Copying FW2 to /lib/firmware...\n");
    char cp_cmd[1024];
    snprintf(cp_cmd, sizeof(cp_cmd), "cp ./mcore_baremetal2.elf /lib/firmware/mcore_baremetal2.elf 2>/dev/null");
    if (system(cp_cmd) == -1) {
        printf("[A-Core] Warning: Failed to copy FW2 to /lib/firmware (may fallback to scenario dir)\n");
    }

    // Disable EN so that FW2 waits in its initialization loop
    regs[5] = 0;

    printf("[A-Core] Setting firmware name to mcore_baremetal2.elf...\n");
    int fw_fd = open("/sys/class/remoteproc/remoteproc0/firmware", O_WRONLY);
    if (fw_fd == -1) {
        perror("Failed to open remoteproc firmware path");
        munmap((void *)regs, REG_SIZE);
        close(uio_fd);
        close(state_fd);
        return 1;
    }
    if (write(fw_fd, "mcore_baremetal2.elf", strlen("mcore_baremetal2.elf")) == -1) {
        perror("Failed to write FW2 name");
        close(fw_fd);
        munmap((void *)regs, REG_SIZE);
        close(uio_fd);
        close(state_fd);
        return 1;
    }
    close(fw_fd);

    printf("[A-Core] Starting FW2...\n");
    write_state("start");

    // Wait a brief moment for FW2 process to spawn
    sleep(1);

    // 6. Reset and Enable counter for FW2
    printf("[A-Core] Resetting counter for FW2...\n");
    regs[4] = 1; // RST
    regs[4] = 0;

    printf("[A-Core] Enabling counter for FW2...\n");
    regs[5] = 1; // EN

    printf("[A-Core] Waiting for FW2 to monitor counter...\n");
    sleep(2);

    uint32_t val2 = regs[6]; // CNT
    printf("[A-Core] Counter value with FW2: %u\n", val2);
    int fw2_success = (val2 > 0);

    // 7. Stop FW2
    printf("[A-Core] Stopping FW2...\n");
    write_state("stop");

    // Wait until remoteproc state becomes offline
    printf("[A-Core] Waiting for remoteproc to become offline...\n");
    while (1) {
        lseek(state_fd, 0, SEEK_SET);
        ssize_t temp_n = read(state_fd, state_buf, sizeof(state_buf) - 1);
        if (temp_n >= 0) {
            state_buf[temp_n] = '\0';
            ssize_t temp_len = temp_n;
            while (temp_len > 0 && (state_buf[temp_len-1] == '\n' || state_buf[temp_len-1] == '\r' || state_buf[temp_len-1] == ' ')) {
                state_buf[temp_len-1] = '\0';
                temp_len--;
            }
        } else {
            state_buf[0] = '\0';
        }
        if (strcmp(state_buf, "offline") == 0) {
            break;
        }
        usleep(50000); // 50ms
    }
    printf("[A-Core] remoteproc state is offline. FW2 stopped successfully!\n");
    close(state_fd);

    // Clean up
    munmap((void *)regs, REG_SIZE);
    close(uio_fd);

    system("rm -f /lib/firmware/mcore_baremetal.elf /lib/firmware/mcore_baremetal2.elf 2>/dev/null");

    printf("--- remoteproc AMP Test Finished ---\n");

    if (fw1_success && fw2_success) {
        printf("[A-Core] remoteproc dynamic loading hot-swap test PASSED!\n");
        return 0;
    } else {
        printf("[A-Core] FAILURE: FW1 success = %d, FW2 success = %d\n", fw1_success, fw2_success);
        return 1;
    }
}
