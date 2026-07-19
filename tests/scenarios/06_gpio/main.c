#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

#include "vfpga_device_config.h"
#define UIO_DEVICE FBB_DEV_PATH_VFPGA_GPIO
#define REG_SIZE 1024

int main() {
    printf("--- AXI GPIO Test Start ---\n");

    int fd = open(UIO_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("open failed");
        return 1;
    }

    volatile uint32_t *regs = mmap(NULL, REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    // AXI GPIO offsets (divided by 4 for uint32_t pointer arithmetic)
    // DATA = 0x00 / 4 = 0
    // TRI  = 0x04 / 4 = 1
    // DATA2 = 0x08 / 4 = 2
    // TRI2  = 0x0C / 4 = 3

    // Set Channel 1 (DATA) as output (TRI = 0x00000000)
    printf("[App] Configuring Channel 1 as output...\n");
    regs[1] = 0x00000000;

    // Set Channel 2 (DATA2) as input (TRI2 = 0xFFFFFFFF)
    printf("[App] Configuring Channel 2 as input...\n");
    regs[3] = 0xFFFFFFFF;

    // Toggle some outputs on Channel 1
    const char *interactive_env = getenv("VFPGA_INTERACTIVE");
    int is_interactive = (interactive_env != NULL && interactive_env[0] == '1');
    
    if (is_interactive) {
        printf("[App] Blinking LEDs on Channel 1. Open the dashboard to view!\n");
        printf("[App] Press Ctrl+C to stop the test.\n");
    } else {
        printf("[App] Automated test mode detected. Running for 5 iterations...\n");
    }
    
    int i = 0;
    while (is_interactive || i < 5) {
        uint32_t out_val = (1 << (i % 8)); // Cycle through 8 bits
        printf("[App] Writing 0x%08X to DATA (Channel 1)...\n", out_val);
        regs[0] = out_val;
        
        // Read from Channel 2
        uint32_t in_val = regs[2];
        printf("[App] Read 0x%08X from DATA2 (Channel 2)...\n", in_val);
        
        sleep(1);
        i++;
    }

    // This part is unreachable in an infinite loop, but good practice
    printf("[App] GPIO Test Complete.\n");

    munmap((void *)regs, REG_SIZE);
    close(fd);
    return 0;
}
