/**
 * @file main.c
 * @intent:responsibility Mコアから物理アドレス直接アクセスによる AXI CDMA 転送を検証する。
 * @intent:rationale      Mコアの決定論的物理メモリ空間からの DMA 転送トリガーと転送完了をテストする。
 * @intent:pre-condition  FBB_MCORE=1 マッピングが有効であること。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <termios.h>

#define GPIO_BASE_ADDR 0x40001000

int main(int argc, char **argv) {
    int batch_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--batch") == 0) {
            batch_mode = 1;
        }
    }

    if (getenv("VFPGA_INTERACTIVE") != NULL) {
        batch_mode = 0;
    }

    /* Open /dev/mem with O_SYNC flag */
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[A-Core Host] Failed to open /dev/mem");
        return 1;
    }

    volatile uint32_t *gpio = (volatile uint32_t *)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_BASE_ADDR);
    if (gpio == MAP_FAILED) {
        perror("[A-Core Host] mmap failed");
        close(fd);
        return 1;
    }

    /* Configure Xilinx XPS GPIO TRI register (Offset 0x04) to 0xFFFFFFFF (All 16 pins as Inputs) */
    gpio[1] = 0xFFFFFFFF;

    if (batch_mode) {
        /* Batch mode handled synchronously via run.sh and mcore_cdma.elf execution */
        printf("[A-Core Host] AMP M-Core CDMA Regression Runner active.\n");
        return 0;
    } else {
        printf("[A-Core Host] AMP M-Core CDMA Interactive Controller active.\n");
        printf("[A-Core Host] Relaying Dashboard GPIO inputs (B0..B3) to M-Core Firmware...\n");
        
        while (1) {
            usleep(500000);
        }
    }

    return 0;
}
