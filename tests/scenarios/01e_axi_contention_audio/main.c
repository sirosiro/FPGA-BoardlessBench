/**
 * @file main.c
 * @brief Scenario 01e: AXI Bus Contention & Audio DMA Latency Chaos Verification
 * @intent:responsibility
 *   実機（Pynq-Z2等）特有の AXI バス帯域競合および PS側 DRAM 可変レイテンシによる
 *   オーディオ FIFO アンダーラン（0.5μs デッドライン違反）の決定論的再現性を検証する。
 * @intent:rationale
 *   - 通常実行（遅延ゼロ/理想Verilator環境）: 1サイクルで即座にデータが返却され、
 *     FIFO は常に満たされ、アンダーランは一切発生しない (underrun == 0)。
 *   - カオス実行 (--chaos --chaos-targets=axi_stall): 8〜32 サイクルの突発的ストールが
 *     発生し、4サンプル FIFO が枯渇してアンダーランが確実に検出される (underrun > 0)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "vfpga_device_config.h"

#ifndef FBB_DEV_PATH_VFPGA_AUDIO
#define FBB_DEV_PATH_VFPGA_AUDIO "/dev/uio0"
#endif

#define UIO_DEV FBB_DEV_PATH_VFPGA_AUDIO
#define UIO_MMAP_SIZE 4096

#define CR_CTRL_OFFSET 0x10
#define SR_STATUS_OFFSET 0x14
#define UNDERRUN_CNT_OFFSET 0x18
#define SAMPLES_PLAYED_OFFSET 0x1C

// External C-shim register access functions
extern void fbb_write_reg(void *base_addr, unsigned long offset, uint32_t val);
extern uint32_t fbb_read_reg(void *base_addr, unsigned long offset);

int main(int argc, char **argv) {
    printf("======================================================================\n");
    printf("[Scenario 01e] AXI Bus Contention & Memory Latency Verification\n");
    printf("======================================================================\n");

    int fd = open(UIO_DEV, O_RDWR);
    if (fd < 0) {
        perror("open UIO_DEV");
        return 1;
    }

    uint32_t *base = (uint32_t *)mmap(NULL, UIO_MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    // 1. Initialize sample buffer in DRAM/SHM
    for (int i = 0; i < UIO_MMAP_SIZE / 4; i++) {
        base[i] = 0xA000 + i;
    }

    // 2. Detect chaos mode from environment
    const char *chaos_env = getenv("FBB_CHAOS_MODE");
    int chaos_active = (chaos_env && (strcmp(chaos_env, "1") == 0 || strcasecmp(chaos_env, "true") == 0));
    const char *seed_env = getenv("FBB_CHAOS_SEED");

    printf("[01e] Environment: Chaos Mode = %s (Seed: %s)\n",
           chaos_active ? "ENABLED (AXI_STALL)" : "DISABLED (Ideal 0-latency)",
           seed_env ? seed_env : "N/A");

    // 3. Reset FIFO
    fbb_write_reg(base, CR_CTRL_OFFSET, 0x02); // FIFO Reset
    usleep(5000);

    // 4. Start Audio Playback (Consumes 1 sample every 4 cycles / 0.5us deadline)
    printf("[01e] Starting audio DMA playback...\n");
    fbb_write_reg(base, CR_CTRL_OFFSET, 0x01); // Enable Audio

    // 5. Let playback run for 25ms
    usleep(25000);

    // 6. Stop playback and read status
    fbb_write_reg(base, CR_CTRL_OFFSET, 0x00); // Disable Audio
    usleep(5000);

    uint32_t status = fbb_read_reg(base, SR_STATUS_OFFSET);
    uint32_t underrun_cnt = fbb_read_reg(base, UNDERRUN_CNT_OFFSET);
    uint32_t samples_played = fbb_read_reg(base, SAMPLES_PLAYED_OFFSET);

    printf("----------------------------------------------------------------------\n");
    printf("[01e] Results:\n");
    printf("      Samples Played: %u\n", samples_played);
    printf("      Underrun Count: %u\n", underrun_cnt);
    printf("      Status Reg:     0x%08X (Active=%d, UnderrunFlag=%d)\n",
           status, status & 0x1, (status >> 1) & 0x1);
    printf("----------------------------------------------------------------------\n");

    munmap(base, UIO_MMAP_SIZE);
    close(fd);

    if (chaos_active) {
        if (underrun_cnt > 0) {
            printf("\033[1;32m[PASS] AXI Bus Contention Successfully Injected & Verified!\033[0m\n");
            printf("       Reproduced real-hardware DRAM latency stalls on PC without physical Pynq-Z2.\n");
            printf("       (Detected %u FIFO starvation underruns during playback)\n", underrun_cnt);
            return 0;
        } else {
            fprintf(stderr, "\033[1;31m[FAIL] Chaos mode was active but 0 underruns occurred!\033[0m\n");
            return 1;
        }
    } else {
        if (underrun_cnt == 0 && samples_played > 0) {
            printf("\033[1;32m[PASS] Ideal Verilator zero-latency baseline verified!\033[0m\n");
            printf("       (0 underruns, %u samples played seamlessly)\n", samples_played);
            return 0;
        } else {
            fprintf(stderr, "\033[1;31m[FAIL] Unexpected underrun in ideal mode: %u\033[0m\n", underrun_cnt);
            return 1;
        }
    }
}
