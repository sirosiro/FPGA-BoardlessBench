#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define REG_RST   (*(volatile uint32_t*)(0x40000010))
#define REG_EN    (*(volatile uint32_t*)(0x40000014))
#define REG_CNT   (*(volatile uint32_t*)(0x40000018))

int main() {
    printf("[M-Core 2] Baremetal firmware (hot-swapped) started.\n");
    fflush(stdout);

    // Wait until EN is enabled by A-Core
    while (!REG_EN) {
        usleep(10000);
    }
    printf("[M-Core 2] EN detected! Starting monitoring loop.\n");
    fflush(stdout);

    // Loop and print CNT value to demonstrate we can read it
    for (int i = 0; i < 5; i++) {
        printf("[M-Core 2] REG_CNT = %u\n", REG_CNT);
        fflush(stdout);
        usleep(200000); // 200ms
    }

    printf("[M-Core 2] Baremetal firmware finished successfully.\n");
    fflush(stdout);
    return 0;
}
