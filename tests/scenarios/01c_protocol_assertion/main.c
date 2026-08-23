#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

#define UIO_DEV "/dev/uio0"
#define GPIO_DEV "/dev/uio1"
#define MAP_SIZE 0x1000

extern void fbb_write_reg(void *base_addr, unsigned long offset, uint32_t val);
extern uint32_t fbb_read_reg(void *base_addr, unsigned long offset);

static void fw_write_reg(void *base_addr, unsigned long offset, uint32_t val) {
    fbb_write_reg(base_addr, offset, val);
}

static uint32_t fw_read_reg(void *base_addr, unsigned long offset) {
    return fbb_read_reg(base_addr, offset);
}

int main() {
    printf("=== F-BB Scenario: 01c_protocol_assertion ===\n");

    // Clean log file
    unlink("/tmp/fbb_protocol_violations.log");

    int fd = open(UIO_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " UIO_DEV);
        return 1;
    }

    uint32_t *vfpga = (uint32_t *)mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (vfpga == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    // Optional GPIO Dashboard Trigger
    int gpio_fd = open(GPIO_DEV, O_RDWR);
    volatile uint32_t *gpio_regs = NULL;
    if (gpio_fd >= 0) {
        gpio_regs = (uint32_t *)mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, gpio_fd, 0);
        if (gpio_regs != MAP_FAILED) {
            gpio_regs[3] = 0xFFFFFFFF; // Set GPIO2 (Channel 2) as input
        }
    }

    const char *interactive_env = getenv("FBB_INTERACTIVE");
    if (!interactive_env) interactive_env = getenv("VFPGA_INTERACTIVE");
    int is_interactive = (interactive_env != NULL && interactive_env[0] == '1');

    if (is_interactive && gpio_regs != NULL && gpio_regs != MAP_FAILED) {
        printf("\n============================================================\n");
        printf("🎯 [FW] INTERACTIVE DASHBOARD MODE ENABLED!\n");
        printf("👉 Open Dashboard (http://localhost:8080)\n");
        printf("👉 Go to 'GPIO / Pin Array' pane and toggle Pin 0 ON to start test!\n");
        printf("============================================================\n\n");

        while ((gpio_regs[2] & 0x1) == 0) {
            usleep(100000); // Poll every 100ms for Dashboard GPIO Pin 0 toggle
        }

        printf("⚡ [FW] START trigger received from Dashboard GPIO Pin 0! Executing test...\n");
    }

    // 1. Valid RW access to CTRL (0x00)
    fw_write_reg(vfpga, 0x00, 0x12345678);
    printf("[FW] Written 0x12345678 to CTRL (RW)\n");

    // 2. Valid RO read from STATUS (0x04)
    uint32_t status_val = fw_read_reg(vfpga, 0x04);
    printf("[FW] Readback STATUS (RO): 0x%08X\n", status_val);

    // 3. Intentionally attempt illegal Write to Read-Only STATUS register (0x04)
    printf("[FW] Attempting illegal WRITE to Read-Only STATUS register...\n");
    fw_write_reg(vfpga, 0x04, 0xDEADBEEF); // Triggers F-BB Protocol Assertion!

    munmap(vfpga, MAP_SIZE);
    close(fd);
    if (gpio_regs && gpio_regs != MAP_FAILED) {
        munmap((void *)gpio_regs, MAP_SIZE);
        close(gpio_fd);
    }

    // 4. Verify violation log was generated
    FILE *f = fopen("/tmp/fbb_protocol_violations.log", "r");
    if (!f) {
        printf("❌ FAILED: Violation log /tmp/fbb_protocol_violations.log not found!\n");
        return 1;
    }

    char line[256];
    int found_violation = 0;
    while (fgets(line, sizeof(line), f)) {
        printf("[Log] %s", line);
        if (strstr(line, "WRITE_TO_RO") && strstr(line, "reg=STATUS")) {
            found_violation = 1;
        }
    }
    fclose(f);

    if (found_violation) {
        printf("=== Scenario 01c_protocol_assertion Test Result: SUCCESS ===\n");
        if (is_interactive) {
            printf("[FW] FBB_INTERACTIVE enabled. Keeping simulation alive for 30s for Dashboard inspection...\n");
            sleep(30);
        }
        return 0;
    } else {
        printf("❌ FAILED: Protocol assertion did not capture WRITE_TO_RO for STATUS!\n");
        return 1;
    }
}
