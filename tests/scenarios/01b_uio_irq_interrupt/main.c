/**
 * @file main.c
 * @intent:responsibility Linux UIO ドライバの非同期割り込み（eventfd 駆動のブロッキング read）と ACK 処理を検証する。
 * @intent:rationale      ポーリングによる CPU 浪費を避け、ハードウェアタイマー割り込み通知メカニズムを実機同等の read() ブロッキング API でテストする。
 * @intent:pre-condition  UIO デバイスノード（/dev/uio0）およびタイマー割り込み生成ロジックが有効であること。
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#define UIO_DEV "/dev/uio0"
#define MMIO_SIZE 0x1000

#define REG_CTRL_OFFSET    0x00
#define REG_STATUS_OFFSET  0x04
#define REG_INT_ACK_OFFSET 0x08
#define REG_CNT_OFFSET     0x0C

int main(int argc, char *argv[]) {
    printf("=== Scenario 01b: UIO Asynchronous Interrupt (IRQ) Test ===\n");

    int fd = open(UIO_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " UIO_DEV);
        return 1;
    }

    volatile uint32_t *regs = (volatile uint32_t *)mmap(NULL, MMIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    printf("[FW Main] Successfully mapped UIO MMIO registers.\n");

    // Enable Hardware Timer
    regs[REG_CTRL_OFFSET / 4] = 0x1;
    printf("[FW Main] Hardware Timer Enabled (CTRL = 1).\n");

    // Unmask UIO Interrupt
    uint32_t unmask = 1;
    if (write(fd, &unmask, sizeof(unmask)) != sizeof(unmask)) {
        perror("uio_unmask (write) failed");
        munmap((void *)regs, MMIO_SIZE);
        close(fd);
        return 1;
    }
    printf("[FW Main] UIO Interrupt Unmasked. Waiting for IRQ via read()...\n");

    // Block on read() until IRQ arrives
    uint32_t irq_count = 0;
    ssize_t read_bytes = read(fd, &irq_count, sizeof(irq_count));
    if (read_bytes != sizeof(irq_count)) {
        fprintf(stderr, "[FW Main] ERROR: read() failed or returned unexpected size: %zd\n", read_bytes);
        munmap((void *)regs, MMIO_SIZE);
        close(fd);
        return 1;
    }

    printf("[FW IRQ Handler] Interrupt Received! Accumulated IRQs: %u\n", irq_count);

    // Read STATUS and ACK the interrupt
    uint32_t status = regs[REG_STATUS_OFFSET / 4];
    uint32_t cnt = regs[REG_CNT_OFFSET / 4];
    printf("[FW IRQ Handler] STATUS reg: 0x%08X, CNT reg: %u\n", status, cnt);

    regs[REG_INT_ACK_OFFSET / 4] = 0x1; // INT ACK
    printf("[FW IRQ Handler] INT_ACK written. IRQ successfully handled.\n");

    // Clean exit
    regs[REG_CTRL_OFFSET / 4] = 0x0; // Stop timer
    munmap((void *)regs, MMIO_SIZE);
    close(fd);

    printf("=== Scenario 01b Test Result: SUCCESS ===\n");
    return 0;
}
