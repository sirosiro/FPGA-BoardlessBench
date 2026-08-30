/**
 * @file main.c
 * @intent:responsibility レガシーな /dev/mem 直接 mmap アクセスによる物理アドレス空間のレジスタ操作を検証する。
 * @intent:rationale      UIO ドライバ非対応の古いファームウェアやベアメタル移行用コードの互換性をテストする。
 * @intent:pre-condition  C-Shim による /dev/mem インターセプトが有効であること。
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

/**
 * 【解説: /dev/mem とレガシーなアクセス】
 * UIOが登場する前、特定のデバイスドライバを書かずにFPGAレジスタを操作する
 * 最も一般的な方法が /dev/mem を介した物理メモリへの直接アクセスでした。
 */
#define MEM_DEVICE "/dev/mem"
#define FPGA_BASE_ADDR 0x40000000  // FPGAの物理ベースアドレス
#define REG_SIZE 1024

// 【解説: レジスタアクセス用マクロ】
// 組み込み開発の古いコードや、ベアメタルに近い環境でよく使われる手法です。
// ポインタ演算を隠蔽し、コードの可読性を高めます。
#define WRITEL(val, base, offset) (*(volatile uint32_t *)((uintptr_t)base + offset) = val)
#define READL(base, offset)       (*(volatile uint32_t *)((uintptr_t)base + offset))

int main() {
    printf("--- /dev/mem Legacy Access Test Start ---\n");

    // 【ステップ1】 /dev/mem を開く
    // 【重要】 実機Linuxでは、/dev/mem へのアクセスには root 権限（sudo等）が必要です。
    printf("[App] Opening %s...\n", MEM_DEVICE);
    int fd = open(MEM_DEVICE, O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("open /dev/mem failed");
        return 1;
    }

    // 【ステップ2】 物理アドレスをプロセスの仮想空間にマッピングする
    // UIOと決定的に違う点は、mmap の第6引数（オフセット）に
    // 直接「物理アドレス (0x40000000)」を指定することです。
    printf("[App] Mapping physical address 0x%08X...\n", FPGA_BASE_ADDR);
    void *virt_base = mmap(NULL, REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FPGA_BASE_ADDR);
    if (virt_base == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    // --- 実装例 A: ポインタ配列としてのアクセス ---
    // uint32_t (4バイト) 型のポインタとして扱う方法です。
    volatile uint32_t *regs = (volatile uint32_t *)virt_base;
    printf("[App] Writing 1 to EN (offset 0x14) using pointer array...\n");
    // オフセット 0x14(20) は、4バイト単位の配列では index 5 に相当します。
    regs[5] = 1; 

    sleep(1);
    uint32_t cnt = regs[6]; // CNTレジスタ (0x18 / 4 = 6)
    printf("[App] Current Counter value: %u\n", cnt);
    if (cnt > 0) {
        printf("[App] SUCCESS: Direct memory access verified!\n");
    }

    // --- 実装例 B: マクロを使用したアクセス ---
    // 物理アドレス + オフセットという形式を維持できるため、
    // 実機の仕様書（データシート）との対応が取りやすい書き方です。
    printf("[App] Resetting counter via WRITEL/READL macros...\n");
    
    WRITEL(1, virt_base, 0x10); // RSTレジスタ(0x10)に1を書き込み (Reset Assert)
    WRITEL(0, virt_base, 0x10); // 0を書き込み (Reset De-assert)
    
    WRITEL(1, virt_base, 0x14); // ENレジスタ(0x14)に1を書き込み (Enable)
    
    printf("[App] Waiting for counter increment...\n");
    uint32_t val1 = READL(virt_base, 0x18);
    sleep(1);
    uint32_t val2 = READL(virt_base, 0x18);
    
    printf("[App] Value 1: %u, Value 2: %u\n", val1, val2);
    if (val2 > val1) {
        printf("[App] SUCCESS: Legacy style access confirmed!\n");
    } else {
        printf("[App] FAILURE: Counter is stuck!\n");
        return 1;
    }

    munmap(virt_base, REG_SIZE);
    close(fd);
    printf("--- /dev/mem Legacy Access Test End ---\n");
    return 0;
}
