/**
 * @file main.c
 * @intent:responsibility Aコア Linux アプリケーション。vfpga_timer と amp_shared_mem の 2 ノード分離構成上で
 *                        Mコア SFS ファームウェアの自動回帰検証および UART 対話操作コンソールを提供する。
 * @intent:rationale      全コマンド送信を send_cmd_sync() でハンドシェイク同期し、バースト投入時の
 *                        パケットロストを防ぎつつ Time-Travel Debugging に最適な波形変化を生成する。
 * @intent:pre-condition  Mコアバイナリ (mcore_sfs.elf) が remoteproc0 経由で起動済みであること。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <stdarg.h>
#include <errno.h>

#include "vfpga_device_config.h"

#define MAP_SIZE 0x1000

/* vfpga_timer@40000000 (/dev/uio0) ワードインデックス */
#define TMR_IDX_CTRL    (0x00 / 4)
#define TMR_IDX_PERIOD  (0x04 / 4)
#define TMR_IDX_STATUS  (0x08 / 4)
#define TMR_IDX_INT_ACK (0x0C / 4)
#define TMR_IDX_HW_CNT  (0x10 / 4)

/* amp_shared_mem@40001000 (/dev/uio1) ワードインデックス */
#define SHM_IDX_CMD             (0x00 / 4)
#define SHM_IDX_STATUS          (0x04 / 4)
#define SHM_IDX_DATA_IN         (0x08 / 4)
#define SHM_IDX_DATA_OUT        (0x0C / 4)
#define SHM_IDX_FRCC_TICKS      (0x10 / 4)
#define SHM_IDX_HEARTBEAT_CNT   (0x14 / 4)
#define SHM_IDX_SM_MODE         (0x18 / 4)
#define SHM_IDX_SM_STATE        (0x1C / 4)
#define SHM_IDX_FIFO_DEPTH      (0x20 / 4)
#define SHM_IDX_FIFO_PROCESSED  (0x24 / 4)
#define SHM_IDX_LAST_TRANSITION (0x28 / 4)
#define SHM_IDX_RB_USED_BYTES   (0x2C / 4)
#define SHM_IDX_RB_DRAINED_CNT  (0x30 / 4)

/* STATUS ビット */
#define STATUS_READY    (1U << 0)
#define STATUS_BUSY     (1U << 1)
#define STATUS_RUNNING  (1U << 2)
#define STATUS_SHUTDOWN (1U << 3)

/* Matrix モード・状態 */
#define MODE_NORMAL     0U
#define MODE_DIAGNOSTIC 1U
#define STATE_IDLE      0U
#define STATE_ACTIVE    1U
#define STATE_SUSPENDED 2U

/* コマンドコード */
#define CMD_SM_START         0x01U
#define CMD_SM_PAUSE         0x02U
#define CMD_SM_RESUME        0x03U
#define CMD_SM_STOP          0x04U
#define CMD_SET_MODE         0x05U
#define CMD_ENQUEUE_DATA     0xA1U
#define CMD_SET_TIMER_PERIOD 0xB1U
#define CMD_SHUTDOWN         0xFFU

static int send_cmd_sync(volatile uint32_t *shm_regs, uint32_t cmd, uint32_t data_in, int timeout_ms)
{
    int elapsed = 0;

    while (shm_regs[SHM_IDX_CMD] != 0U) {
        if (elapsed >= timeout_ms) {
            return -1;
        }
        usleep(1000);
        elapsed++;
    }

    shm_regs[SHM_IDX_DATA_IN] = data_in;
    __sync_synchronize();
    shm_regs[SHM_IDX_CMD] = cmd;
    __sync_synchronize();

    while (shm_regs[SHM_IDX_CMD] != 0U) {
        if (elapsed >= timeout_ms) {
            return -1;
        }
        usleep(1000);
        elapsed++;
    }

    return 0;
}

static int wait_for_mcore_boot(volatile uint32_t *timer_regs, volatile uint32_t *shm_regs, int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        uint32_t status = shm_regs[SHM_IDX_STATUS];
        uint32_t hw_cnt = timer_regs[TMR_IDX_HW_CNT];
        uint32_t frcc   = shm_regs[SHM_IDX_FRCC_TICKS];
        uint32_t hb     = shm_regs[SHM_IDX_HEARTBEAT_CNT];
        if ((status & STATUS_RUNNING) && hw_cnt >= 2U && frcc >= 2U && hb >= 1U) {
            return 0;
        }
        usleep(10000);
        elapsed += 10;
    }
    return -1;
}

static const char *mode_name(uint32_t m)
{
    return (m == MODE_DIAGNOSTIC) ? "DIAGNOSTIC" : "NORMAL";
}

static const char *state_name(uint32_t s)
{
    if (s == STATE_ACTIVE) return "ACTIVE";
    if (s == STATE_SUSPENDED) return "SUSPENDED";
    return "IDLE";
}

static void uart_write_all(int uart_fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(uart_fd, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        sent += (size_t)n;
    }
}

static void uart_printf(int uart_fd, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    int len;
    if (uart_fd < 0) return;
    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        size_t write_len = ((size_t)len >= sizeof(buf)) ? (sizeof(buf) - 1U) : (size_t)len;
        uart_write_all(uart_fd, buf, write_len);
    }
}

static void print_uart_menu(int uart_fd)
{
    const char *menu =
        "\r\n"
        "============================================================\r\n"
        "  F-BB Scenario 23: M-Core SFS + Hardware Timer IRQ Shell\r\n"
        "============================================================\r\n"
        "  [State Machine (Matrix)]\r\n"
        "    1. Start   (IDLE -> ACTIVE)\r\n"
        "    2. Pause   (ACTIVE -> SUSPENDED)\r\n"
        "    3. Resume  (SUSPENDED -> ACTIVE)\r\n"
        "    4. Stop    (-> IDLE)\r\n"
        "    5. Toggle Mode (NORMAL <-> DIAGNOSTIC)\r\n"
        "  [FIFO & Data Pipeline]\r\n"
        "    6. Send Single Data Packet (DATA_IN = next value)\r\n"
        "    7. Burst Enqueue 4x Packets (Observe FIFO_DEPTH & FRCC!)\r\n"
        "  [Hardware Timer & Telemetry]\r\n"
        "    8. Cycle Timer Period (500 Fast / 1500 Normal / 3000 Slow)\r\n"
        "    9. Show Full Telemetry Snapshot (HW Timer / FRCC / SM / FIFO / RB)\r\n"
        "    0. Shutdown M-Core & Exit\r\n"
        "------------------------------------------------------------\r\n"
        "Select (0-9): ";
    uart_write_all(uart_fd, menu, strlen(menu));
}

static void print_telemetry_snapshot(int uart_fd, volatile uint32_t *timer_regs, volatile uint32_t *shm_regs)
{
    uint32_t trans = shm_regs[SHM_IDX_LAST_TRANSITION];
    uart_printf(uart_fd,
        "\r\n--- Telemetry Snapshot ---\r\n"
        "  [vfpga_timer]    CTRL=0x%X PERIOD=%u STATUS=0x%X HW_CNT=%u\r\n"
        "  [amp_shared_mem] STATUS=0x%X FRCC_TICKS=%u HB_CNT=%u\r\n"
        "  [Matrix SM]      MODE=%s(%u) STATE=%s(%u)\r\n"
        "  [FIFO & Data]    DEPTH=%u PROCESSED=%u DATA_OUT=%u\r\n"
        "  [Ring Buffer]    USED=%uB DRAINED=%u LAST_TRANS=0x%08X (m=%u,%u->%u ev=%u)\r\n",
        timer_regs[TMR_IDX_CTRL],
        timer_regs[TMR_IDX_PERIOD],
        timer_regs[TMR_IDX_STATUS],
        timer_regs[TMR_IDX_HW_CNT],
        shm_regs[SHM_IDX_STATUS],
        shm_regs[SHM_IDX_FRCC_TICKS],
        shm_regs[SHM_IDX_HEARTBEAT_CNT],
        mode_name(shm_regs[SHM_IDX_SM_MODE]), shm_regs[SHM_IDX_SM_MODE],
        state_name(shm_regs[SHM_IDX_SM_STATE]), shm_regs[SHM_IDX_SM_STATE],
        shm_regs[SHM_IDX_FIFO_DEPTH],
        shm_regs[SHM_IDX_FIFO_PROCESSED],
        shm_regs[SHM_IDX_DATA_OUT],
        shm_regs[SHM_IDX_RB_USED_BYTES],
        shm_regs[SHM_IDX_RB_DRAINED_CNT],
        trans,
        (trans >> 24) & 0xFFU,
        (trans >> 16) & 0xFFU,
        trans & 0xFFU,
        (trans >> 8) & 0xFFU);
}

static int run_interactive_mode(volatile uint32_t *timer_regs, volatile uint32_t *shm_regs)
{
    int uart_fd = open(FBB_DEV_PATH_VFPGA_UART, O_RDWR | O_NOCTTY);
    if (uart_fd < 0) {
        perror("[A-Core] Failed to open UART " FBB_DEV_PATH_VFPGA_UART);
        return 1;
    }

    struct termios options;
    tcgetattr(uart_fd, &options);
    cfmakeraw(&options);
    tcsetattr(uart_fd, TCSANOW, &options);

    /* Lab 対話観察向けに 200ms Tracer ポーリングへ見やすい周期 (1500クロック) へ調整 */
    send_cmd_sync(shm_regs, CMD_SET_TIMER_PERIOD, 1500U, 5000);

    printf("[A-Core] Interactive Lab Mode Ready (UART: %s, TIMER_PERIOD=1500)\n", FBB_DEV_PATH_VFPGA_UART);
    fflush(stdout);

    print_uart_menu(uart_fd);

    uint32_t next_data = 10U;
    uint32_t periods[3] = { 1500U, 3000U, 500U };
    int period_idx = 0;

    while (1) {
        fd_set rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(uart_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int sel = select(uart_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(uart_fd, &rfds)) {
            char ch = '\0';
            if (read(uart_fd, &ch, 1) <= 0) {
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                continue;
            }
            uart_printf(uart_fd, "%c\r\n", ch);

            if (ch == '1') {
                send_cmd_sync(shm_regs, CMD_SM_START, 0U, 5000);
                uart_printf(uart_fd, "[OK] SM_START -> State=%s\r\n", state_name(shm_regs[SHM_IDX_SM_STATE]));
            } else if (ch == '2') {
                send_cmd_sync(shm_regs, CMD_SM_PAUSE, 0U, 5000);
                uart_printf(uart_fd, "[OK] SM_PAUSE -> State=%s\r\n", state_name(shm_regs[SHM_IDX_SM_STATE]));
            } else if (ch == '3') {
                send_cmd_sync(shm_regs, CMD_SM_RESUME, 0U, 5000);
                uart_printf(uart_fd, "[OK] SM_RESUME -> State=%s\r\n", state_name(shm_regs[SHM_IDX_SM_STATE]));
            } else if (ch == '4') {
                send_cmd_sync(shm_regs, CMD_SM_STOP, 0U, 5000);
                uart_printf(uart_fd, "[OK] SM_STOP -> State=%s\r\n", state_name(shm_regs[SHM_IDX_SM_STATE]));
            } else if (ch == '5') {
                uint32_t new_mode = (shm_regs[SHM_IDX_SM_MODE] == MODE_NORMAL) ? MODE_DIAGNOSTIC : MODE_NORMAL;
                send_cmd_sync(shm_regs, CMD_SET_MODE, new_mode, 5000);
                uart_printf(uart_fd, "[OK] Mode switched -> %s\r\n", mode_name(shm_regs[SHM_IDX_SM_MODE]));
            } else if (ch == '6') {
                uint32_t val = next_data;
                next_data += 10U;
                send_cmd_sync(shm_regs, CMD_ENQUEUE_DATA, val, 5000);
                uart_printf(uart_fd, "[OK] Enqueued DATA_IN=%u (FIFO_DEPTH=%u)\r\n", val, shm_regs[SHM_IDX_FIFO_DEPTH]);
            } else if (ch == '7') {
                for (int i = 0; i < 4; i++) {
                    uint32_t val = next_data;
                    next_data += 10U;
                    send_cmd_sync(shm_regs, CMD_ENQUEUE_DATA, val, 5000);
                }
                uart_printf(uart_fd, "[OK] Burst 4x enqueued (FIFO_DEPTH=%u, STATE=%s)\r\n",
                            shm_regs[SHM_IDX_FIFO_DEPTH], state_name(shm_regs[SHM_IDX_SM_STATE]));
            } else if (ch == '8') {
                period_idx = (period_idx + 1) % 3;
                send_cmd_sync(shm_regs, CMD_SET_TIMER_PERIOD, periods[period_idx], 5000);
                uart_printf(uart_fd, "[OK] TIMER_PERIOD=%u clocks\r\n", timer_regs[TMR_IDX_PERIOD]);
            } else if (ch == '9') {
                print_telemetry_snapshot(uart_fd, timer_regs, shm_regs);
            } else if (ch == '0' || ch == 'q') {
                uart_printf(uart_fd, "[A-Core] Shutting down M-Core SFS firmware...\r\n");
                send_cmd_sync(shm_regs, CMD_SHUTDOWN, 0U, 5000);
                break;
            } else {
                uart_printf(uart_fd, "[Warn] Unknown selection '%c'.\r\n", ch);
            }
            print_uart_menu(uart_fd);
        }
    }

    close(uart_fd);
    return 0;
}

static int run_batch_verification(volatile uint32_t *timer_regs, volatile uint32_t *shm_regs)
{
    printf("[A-Core] [1/5] Verifying HW Timer IRQ & FRCC progression...\n");
    printf("[A-Core]       TIMER_HW_CNT=%u, FRCC_TICKS=%u, HEARTBEAT_CNT=%u\n",
           timer_regs[TMR_IDX_HW_CNT], shm_regs[SHM_IDX_FRCC_TICKS], shm_regs[SHM_IDX_HEARTBEAT_CNT]);

    /* 2. Matrix State Transition (IDLE -> ACTIVE) & Ring Buffer drain */
    printf("[A-Core] [2/5] Sending CMD_SM_START and checking Ring Buffer transition log...\n");
    if (send_cmd_sync(shm_regs, CMD_SM_START, 0U, 5000) != 0) {
        fprintf(stderr, "[A-Core] ERROR: CMD_SM_START handshake timed out.\n");
        return 1;
    }
    if (shm_regs[SHM_IDX_SM_STATE] != STATE_ACTIVE) {
        fprintf(stderr, "[A-Core] ERROR: Expected SM_STATE=1 (ACTIVE), got %u.\n", shm_regs[SHM_IDX_SM_STATE]);
        return 1;
    }
    int t = 0;
    while (shm_regs[SHM_IDX_RB_DRAINED_CNT] < 1U && t < 5000) {
        usleep(10000);
        t += 10;
    }
    if (shm_regs[SHM_IDX_RB_DRAINED_CNT] < 1U || shm_regs[SHM_IDX_LAST_TRANSITION] != 0x00000001U) {
        fprintf(stderr, "[A-Core] ERROR: Ring Buffer transition mismatch (drained=%u, last=0x%08X).\n",
                shm_regs[SHM_IDX_RB_DRAINED_CNT], shm_regs[SHM_IDX_LAST_TRANSITION]);
        return 1;
    }
    printf("[A-Core]       SM_STATE=ACTIVE, LAST_TRANSITION=0x%08X (PASS)\n", shm_regs[SHM_IDX_LAST_TRANSITION]);

    /* 3. Pause -> Burst 3x FIFO -> Resume */
    printf("[A-Core] [3/5] Testing PAUSE -> Burst 3x FIFO -> RESUME drain...\n");
    if (send_cmd_sync(shm_regs, CMD_SM_PAUSE, 0U, 5000) != 0 || shm_regs[SHM_IDX_SM_STATE] != STATE_SUSPENDED) {
        fprintf(stderr, "[A-Core] ERROR: Failed to enter STATE_SUSPENDED.\n");
        return 1;
    }
    uint32_t inputs[3] = { 10U, 20U, 30U };
    for (int i = 0; i < 3; i++) {
        if (send_cmd_sync(shm_regs, CMD_ENQUEUE_DATA, inputs[i], 5000) != 0) {
            fprintf(stderr, "[A-Core] ERROR: CMD_ENQUEUE_DATA #%d failed.\n", i);
            return 1;
        }
    }
    if (shm_regs[SHM_IDX_FIFO_DEPTH] != 3U) {
        fprintf(stderr, "[A-Core] ERROR: Expected FIFO_DEPTH=3 while SUSPENDED, got %u.\n", shm_regs[SHM_IDX_FIFO_DEPTH]);
        return 1;
    }
    printf("[A-Core]       SUSPENDED FIFO_DEPTH=%u verified. Resuming...\n", shm_regs[SHM_IDX_FIFO_DEPTH]);
    if (send_cmd_sync(shm_regs, CMD_SM_RESUME, 0U, 5000) != 0 || shm_regs[SHM_IDX_SM_STATE] != STATE_ACTIVE) {
        fprintf(stderr, "[A-Core] ERROR: Failed to resume to STATE_ACTIVE.\n");
        return 1;
    }
    t = 0;
    while (shm_regs[SHM_IDX_FIFO_PROCESSED] < 3U && t < 10000) {
        usleep(10000);
        t += 10;
    }
    if (shm_regs[SHM_IDX_FIFO_PROCESSED] != 3U || shm_regs[SHM_IDX_FIFO_DEPTH] != 0U || shm_regs[SHM_IDX_DATA_OUT] != 91U) {
        fprintf(stderr, "[A-Core] ERROR: Resume drain mismatch (processed=%u, depth=%u, out=%u).\n",
                shm_regs[SHM_IDX_FIFO_PROCESSED], shm_regs[SHM_IDX_FIFO_DEPTH], shm_regs[SHM_IDX_DATA_OUT]);
        return 1;
    }
    printf("[A-Core]       PROCESSED=%u, FIFO_DEPTH=%u, DATA_OUT=%u (PASS)\n",
           shm_regs[SHM_IDX_FIFO_PROCESSED], shm_regs[SHM_IDX_FIFO_DEPTH], shm_regs[SHM_IDX_DATA_OUT]);

    /* 4. Mode switch to DIAGNOSTIC */
    printf("[A-Core] [4/5] Testing MODE_DIAGNOSTIC (val * 10)...\n");
    if (send_cmd_sync(shm_regs, CMD_SET_MODE, MODE_DIAGNOSTIC, 5000) != 0 || shm_regs[SHM_IDX_SM_MODE] != MODE_DIAGNOSTIC) {
        fprintf(stderr, "[A-Core] ERROR: Failed to switch to MODE_DIAGNOSTIC.\n");
        return 1;
    }
    if (send_cmd_sync(shm_regs, CMD_ENQUEUE_DATA, 42U, 5000) != 0) {
        fprintf(stderr, "[A-Core] ERROR: CMD_ENQUEUE_DATA (42) failed.\n");
        return 1;
    }
    t = 0;
    while (shm_regs[SHM_IDX_FIFO_PROCESSED] < 4U && t < 10000) {
        usleep(10000);
        t += 10;
    }
    if (shm_regs[SHM_IDX_FIFO_PROCESSED] != 4U || shm_regs[SHM_IDX_DATA_OUT] != 420U) {
        fprintf(stderr, "[A-Core] ERROR: DIAGNOSTIC output mismatch (processed=%u, out=%u).\n",
                shm_regs[SHM_IDX_FIFO_PROCESSED], shm_regs[SHM_IDX_DATA_OUT]);
        return 1;
    }
    printf("[A-Core]       SM_MODE=DIAGNOSTIC, DATA_OUT=%u (PASS)\n", shm_regs[SHM_IDX_DATA_OUT]);

    /* 5. Stop & Clean Shutdown */
    printf("[A-Core] [5/5] Verifying Stop & Shutdown...\n");
    if (send_cmd_sync(shm_regs, CMD_SM_STOP, 0U, 5000) != 0 || shm_regs[SHM_IDX_SM_STATE] != STATE_IDLE) {
        fprintf(stderr, "[A-Core] ERROR: Failed to return to STATE_IDLE.\n");
        return 1;
    }
    t = 0;
    while (shm_regs[SHM_IDX_RB_DRAINED_CNT] < 4U && t < 5000) {
        usleep(10000);
        t += 10;
    }
    if (shm_regs[SHM_IDX_RB_DRAINED_CNT] < 4U) {
        fprintf(stderr, "[A-Core] ERROR: Expected RB_DRAINED_CNT >= 4, got %u.\n", shm_regs[SHM_IDX_RB_DRAINED_CNT]);
        return 1;
    }
    if (send_cmd_sync(shm_regs, CMD_SHUTDOWN, 0U, 5000) != 0) {
        fprintf(stderr, "[A-Core] ERROR: CMD_SHUTDOWN failed.\n");
        return 1;
    }
    printf("[A-Core]       RB_DRAINED_CNT=%u, STATUS=0x%X (PASS)\n",
           shm_regs[SHM_IDX_RB_DRAINED_CNT], shm_regs[SHM_IDX_STATUS]);
    printf("[A-Core] SUCCESS: Scenario 23 M-Core SFS full-stack verification PASSED!\n");
    return 0;
}

int main(void)
{
    printf("=== Scenario 23: AMP M-Core Simple Functions Scheduler Test Start ===\n");

    int timer_fd = open(FBB_DEV_PATH_VFPGA_TIMER, O_RDWR);
    if (timer_fd < 0) {
        perror("[A-Core] Failed to open " FBB_DEV_PATH_VFPGA_TIMER);
        return 1;
    }
    volatile uint32_t *timer_regs = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, timer_fd, 0);
    if (timer_regs == MAP_FAILED) {
        perror("[A-Core] mmap timer failed");
        close(timer_fd);
        return 1;
    }

    int shm_fd = open(FBB_DEV_PATH_AMP_SHARED_MEM, O_RDWR);
    if (shm_fd < 0) {
        perror("[A-Core] Failed to open " FBB_DEV_PATH_AMP_SHARED_MEM);
        munmap((void *)timer_regs, MAP_SIZE);
        close(timer_fd);
        return 1;
    }
    volatile uint32_t *shm_regs = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_regs == MAP_FAILED) {
        perror("[A-Core] mmap amp_shared_mem failed");
        close(shm_fd);
        munmap((void *)timer_regs, MAP_SIZE);
        close(timer_fd);
        return 1;
    }

    printf("[A-Core] Waiting for M-Core SFS boot & HW timer interrupts...\n");
    if (wait_for_mcore_boot(timer_regs, shm_regs, 10000) != 0) {
        fprintf(stderr, "[A-Core] ERROR: Timed out waiting for M-Core boot (STATUS=0x%X, HW_CNT=%u, FRCC=%u, HB=%u).\n",
                shm_regs[SHM_IDX_STATUS], timer_regs[TMR_IDX_HW_CNT],
                shm_regs[SHM_IDX_FRCC_TICKS], shm_regs[SHM_IDX_HEARTBEAT_CNT]);
        munmap((void *)shm_regs, MAP_SIZE);
        close(shm_fd);
        munmap((void *)timer_regs, MAP_SIZE);
        close(timer_fd);
        return 1;
    }

    const char *interactive_env = getenv("FBB_INTERACTIVE");
    if (!interactive_env) {
        interactive_env = getenv("VFPGA_INTERACTIVE");
    }
    int interactive = (interactive_env && (strcmp(interactive_env, "1") == 0 || strcmp(interactive_env, "true") == 0));

    int ret = interactive
                  ? run_interactive_mode(timer_regs, shm_regs)
                  : run_batch_verification(timer_regs, shm_regs);

    munmap((void *)shm_regs, MAP_SIZE);
    close(shm_fd);
    munmap((void *)timer_regs, MAP_SIZE);
    close(timer_fd);
    return ret;
}
