/**
 * @file main.c
 * @brief Scenario 01d: Automated Chaos & Fault Injection Verification
 * @intent:responsibility
 *   カオス・故障注入エンジン（F-BB Chaos Engine）による I2C NACK 等の障害発生下で、
 *   ファームウェアのリトライ・耐障害性ロジックが正常にフォールトを検知・復旧できるかを検証する。
 * @intent:rationale
 *   通常時は 100% PASS し、カオスモード有効時はシードに基づく決定的な障害注入が行われ、
 *   リトライ機構により通信復旧できることを実証する。
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define UIO_DEV "/dev/uio0"
#define I2C_DEV "/dev/i2c-0"
#define UART_DEV "/dev/ttyUL0"
#define EEPROM_ADDR 0x50
#define MAX_RETRIES 5

static int s_uart_fd = -1;

static void console_print(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        fputs(buf, stdout);
        fflush(stdout);

        if (s_uart_fd >= 0) {
            write(s_uart_fd, buf, len);
        }
    }
}

#define printf(...) console_print(__VA_ARGS__)

static int i2c_read_byte_with_retry(int fd, uint8_t addr, uint8_t *data, int max_retries, int *recovered_faults) {
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        uint8_t buf[1] = {0};
        struct i2c_msg msgs[1];
        struct i2c_rdwr_ioctl_data msgset;

        msgs[0].addr = addr;
        msgs[0].flags = I2C_M_RD;
        msgs[0].len = 1;
        msgs[0].buf = buf;

        msgset.msgs = msgs;
        msgset.nmsgs = 1;

        int ret = ioctl(fd, I2C_RDWR, &msgset);
        if (ret >= 0) {
            *data = buf[0];
            if (attempt > 0) {
                printf("  [RECOVERED] Transaction succeeded on retry attempt %d/%d!\n", attempt, max_retries);
            }
            return 0; // Success
        }

        // Fault detected (e.g. ENXIO from chaos injection)
        (*recovered_faults)++;
        printf("  [WARN] I2C fault encountered on attempt %d (errno=%d: %s). Executing retry...\n",
               attempt + 1, errno, strerror(errno));
        usleep(2000); // 2ms backoff
    }
    return -1; // Exhausted
}

int main(int argc, char **argv) {
    // Open UART device if available for Dashboard UART Console
    s_uart_fd = open(UART_DEV, O_RDWR | O_NOCTTY);
    if (s_uart_fd >= 0) {
        usleep(50000); // 50ms to allow persistent UART bridge to attach to new PTY
    }

    int strict_fail = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--strict-fail-on-chaos") == 0 || strcmp(argv[i], "--no-retry") == 0) {
            strict_fail = 1;
        }
    }

    const char *chaos_env = getenv("FBB_CHAOS_MODE");
    const char *seed_env = getenv("FBB_CHAOS_SEED");
    int chaos_active = (chaos_env && (strcmp(chaos_env, "1") == 0 || strcasecmp(chaos_env, "true") == 0));

    printf("================================================================================\n");
    printf("  F-BB Scenario 01d: Automated Chaos & Fault Injection Resilience Test\n");
    printf("================================================================================\n");
    printf("[CONFIG] Chaos Engine Status: %s\n", chaos_active ? "\033[1;35mACTIVE\033[0m" : "DISABLED (Normal Mode)");
    if (chaos_active && seed_env) {
        printf("[CONFIG] Deterministic PRNG Seed: %s\n", seed_env);
    }

    // Step 1: UIO Memory Mapping & Basic Register Verification
    printf("\n[STEP 1] Verifying UIO MMIO Access...\n");
    int uio_fd = open(UIO_DEV, O_RDWR);
    if (uio_fd < 0) {
        perror("Failed to open " UIO_DEV);
        return 1;
    }
    volatile uint32_t *uio_regs = (volatile uint32_t *)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);
    if (uio_regs == MAP_FAILED) {
        perror("mmap failed");
        close(uio_fd);
        return 1;
    }

    uint32_t status_val = uio_regs[1]; // STATUS @ 0x04
    printf("[UIO] Read STATUS register: 0x%08X (Expected: 0xA5A50001)\n", status_val);
    if (status_val != 0xA5A50001) {
        fprintf(stderr, "❌ Error: STATUS register mismatch!\n");
        munmap((void *)uio_regs, 0x1000);
        close(uio_fd);
        return 1;
    }
    printf("  -> UIO MMIO check PASSED.\n");

    // Step 2: I2C Access with Fault Recovery
    printf("\n[STEP 2] Opening I2C Bus %s for Resilient Communication...\n", I2C_DEV);
    int i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open " I2C_DEV);
        munmap((void *)uio_regs, 0x1000);
        close(uio_fd);
        return 1;
    }

    int total_transactions = 10;
    int recovered_faults = 0;
    int failed_transactions = 0;
    int max_retries = strict_fail ? 0 : MAX_RETRIES;

    printf("[I2C] Executing %d transactions with max_retries=%d...\n", total_transactions, max_retries);
    for (int t = 0; t < total_transactions; t++) {
        uint8_t data = 0;
        int res = i2c_read_byte_with_retry(i2c_fd, EEPROM_ADDR, &data, max_retries, &recovered_faults);
        if (res < 0) {
            printf("❌ [FAIL] Transaction %d/%d failed permanently after %d retries!\n", t + 1, total_transactions, max_retries);
            failed_transactions++;
        }
        usleep(30000); // 30ms spacing for visible real-time progress on UART console & Web UI
    }

    close(i2c_fd);
    munmap((void *)uio_regs, 0x1000);
    close(uio_fd);
    if (s_uart_fd >= 0) {
        close(s_uart_fd);
        s_uart_fd = -1;
    }

    printf("\n================================================================================\n");
    printf("  Test Summary & Resilience Report\n");
    printf("================================================================================\n");
    printf("  Total Transactions Attempted : %d\n", total_transactions);
    printf("  Injected Faults Absorbed     : %d\n", recovered_faults);
    printf("  Permanent Failures           : %d\n", failed_transactions);

    if (strict_fail && recovered_faults > 0 && failed_transactions > 0) {
        printf("\n\033[1;33m[VERIFICATION SUCCESS] Zero-retry mode correctly failed on chaos fault!\033[0m\n");
        return 2; // Expected failure for zero-retry test
    }

    if (failed_transactions > 0) {
        printf("\n\033[1;31m❌ TEST FAILED: Faults exceeded retry capacity!\033[0m\n");
        return 1;
    }

    printf("\n\033[1;32m✅ TEST PASSED: All transactions succeeded %s!\033[0m\n",
           recovered_faults > 0 ? "(faults were successfully recovered via retry)" : "(no faults occurred)");
    return 0;
}
