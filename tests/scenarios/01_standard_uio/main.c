#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

#include "vfpga_device_config.h"
#define UIO_DEVICE FBB_DEV_PATH_VFPGA_REG
#define REG_SIZE 1024

int main() {
    printf("--- Standard UIO and Verilator Sync Test Start ---\n");

    printf("[App] Opening %s...\n", UIO_DEVICE);
    int fd = open(UIO_DEVICE, O_RDWR);
    if (fd == -1) {
        perror("open failed");
        return 1;
    }

    printf("[App] デバイスメモリのマッピング中...\n");
    // 【解説】 mmapとは何か？（何をマッピングしているのか）
    // FPGA上の物理メモリ空間（config.dtsの `reg = <0x40000000 0x1000>` で定義された領域）を、
    // このLinuxアプリケーションから直接読み書きできるように仮想メモリ空間へ「割り当て（マッピング）」しています。
    // これにより、C言語のポインタ（regs配列）に値を代入するだけで、自動的にFPGAのレジスタ（vfpga_top.v）へ信号が送られます。
    //
    // また、ハードウェアのレジスタアクセスでは、コンパイラによる最適化（キャッシュの再利用など）を防ぎ、
    // 毎回必ず物理的なハードウェアへアクセスさせるために「volatile」修飾子を付けるのが鉄則です。
    volatile uint32_t *regs = mmap(NULL, REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    // Basic Read/Write (実在するレジスタ EN でテスト)
    regs[5] = 0x12345678;
    if (regs[5] == 0x12345678) {
        printf("[App] SUCCESS: Basic register R/W verified (EN).\n");
    }

    // Unmapped Address Read Test (未定義アドレスからの読み出し)
    if (regs[0] == 0xdeadbeef) {
        printf("[App] SUCCESS: Unmapped address returned 0xdeadbeef.\n");
    }

    // 【解説: オフセット計算について】
    // regsは uint32_t (4バイト) 型のポインタです。C言語のポインタ演算の仕様により、
    // regs[1] は先頭から 4バイト目、regs[4] は 4 × 4 = 16バイト目 (16進数で 0x10) となります。
    // ペリフェラルの仕様書（DTS）にあるオフセット値を 4 で割った値がインデックスになります。

    printf("[App] 0x10(RST) 経由でカウンターをリセットします...\n");
    regs[4] = 1; // 0x10 (16) / 4 = 4
    regs[4] = 0;

    printf("[App] 0x14(EN) 経由でカウンターを有効化します...\n");
    regs[5] = 1; // 0x14 (20) / 4 = 5

    printf("[App] カウンターの増加を待機中 (Verilator/FPGA側で処理)...\n");
    sleep(1);
    uint32_t val1 = regs[6]; // 0x18 (24) / 4 = 6
    sleep(1);
    uint32_t val2 = regs[6];

    printf("[App] Counter value 1: %u, Value 2: %u\n", val1, val2);
    if (val2 > val1) {
        printf("[App] SUCCESS: Verilator logic (counter) is running and synced!\n");
    } else {
        printf("[App] FAILURE: Counter is not moving. Check RTL/Simulator.\n");
        return 1;
    }

    munmap((void *)regs, REG_SIZE);
    close(fd);
    return 0;
}
