/**
 * @file mcore_sfs.c
 * @intent:responsibility Mコアファームウェア本体。SFS 全 5 コンポーネント (SFS/FRCC/FIFO/Matrix/RingBuffer) を
 *                        RTL ハードウェアタイマー割り込み駆動で協調実行する。
 * @intent:rationale      pthread やホスト依存 API を排除し、実機 Zynq Cortex-R5/M コアと F-BB で
 *                        同一ソースが動作する 100% 実機透過アーキテクチャを実現する。
 * @intent:pre-condition  vfpga_timer@40000000 と amp_shared_mem@40001000 がマッピング済みであること。
 */
#include <stdint.h>
#include <stddef.h>

#include "sfs.h"
#include "frcc.h"
#include "fifo.h"
#include "state_machine.h"
#include "sm_config.h"
#include "ring_buffer.h"
#include "mcore_irq_bsp.h"

/* --------------------------------------------------------------------------
 * 1. FPGA Hardware Timer IP (vfpga_timer @ 0x40000000)
 * -------------------------------------------------------------------------- */
#define TIMER_BASE          0x40000000UL
#define REG_TIMER_CTRL      (*(volatile uint32_t *)(uintptr_t)(TIMER_BASE + 0x00UL))
#define REG_TIMER_PERIOD    (*(volatile uint32_t *)(uintptr_t)(TIMER_BASE + 0x04UL))
#define REG_TIMER_STATUS    (*(volatile uint32_t *)(uintptr_t)(TIMER_BASE + 0x08UL))
#define REG_TIMER_INT_ACK   (*(volatile uint32_t *)(uintptr_t)(TIMER_BASE + 0x0CUL))
#define REG_TIMER_HW_CNT    (*(volatile uint32_t *)(uintptr_t)(TIMER_BASE + 0x10UL))

/* --------------------------------------------------------------------------
 * 2. AMP Shared Memory (amp_shared_mem @ 0x40001000, OCM / AXI BRAM 相当)
 * -------------------------------------------------------------------------- */
#define SHM_BASE            0x40001000UL
#define REG_CMD             (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x00UL))
#define REG_STATUS          (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x04UL))
#define REG_DATA_IN         (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x08UL))
#define REG_DATA_OUT        (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x0CUL))
#define REG_FRCC_TICKS      (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x10UL))
#define REG_HEARTBEAT_CNT   (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x14UL))
#define REG_SM_MODE         (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x18UL))
#define REG_SM_STATE        (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x1CUL))
#define REG_FIFO_DEPTH      (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x20UL))
#define REG_FIFO_PROCESSED  (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x24UL))
#define REG_LAST_TRANSITION (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x28UL))
#define REG_RB_USED_BYTES   (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x2CUL))
#define REG_RB_DRAINED_CNT  (*(volatile uint32_t *)(uintptr_t)(SHM_BASE + 0x30UL))

/* STATUS ビット定義 */
#define STATUS_READY         (1U << 0)
#define STATUS_BUSY          (1U << 1)
#define STATUS_RUNNING       (1U << 2)
#define STATUS_SHUTDOWN      (1U << 3)
#define STATUS_FIFO_OVERFLOW (1U << 4)

/* Aコア -> Mコア コマンドコード */
#define CMD_SM_START         0x01U
#define CMD_SM_PAUSE         0x02U
#define CMD_SM_RESUME        0x03U
#define CMD_SM_STOP          0x04U
#define CMD_SET_MODE         0x05U
#define CMD_ENQUEUE_DATA     0xA1U
#define CMD_SET_TIMER_PERIOD 0xB1U
#define CMD_SHUTDOWN         0xFFU

#define CMD_FIFO_CAPACITY    16U
#define LOG_RB_SIZE          64U

/* --------------------------------------------------------------------------
 * 3. SFS コンポーネント用静的メモリ領域 (動的確保ゼロ)
 * -------------------------------------------------------------------------- */
static struct FIFO_cb g_cmd_fifo;
static long g_fifo_buf[CMD_FIFO_CAPACITY];

static ring_buffer_t g_log_rb;
static unsigned char g_rb_buf[LOG_RB_SIZE];

static SM_Handle_t g_sm;
static FRC g_work_frc;
static FRC g_hb_frc;

static uint8_t g_worker_busy = 0;
static long g_worker_val = 0;
static uint8_t g_shutdown_requested = 0;
static unsigned long g_last_drain_tick = 0;

/* --------------------------------------------------------------------------
 * 4. ハードウェアタイマー割り込みサービスルーチン (非同期シグナル安全)
 * -------------------------------------------------------------------------- */
static void on_timer_isr(void)
{
    gFreeRunCounter++;
    gFreeRunCounterMini++;
    REG_TIMER_INT_ACK = 1U;
}

/* --------------------------------------------------------------------------
 * 5. Matrix 状態機械テーブル＆Ring Buffer 遷移ログ記録コールバック
 * -------------------------------------------------------------------------- */
static void on_sm_transition(uint8_t mode, uint8_t old_state, uint8_t event, uint8_t new_state)
{
    unsigned char entry[4];
    entry[0] = (unsigned char)mode;
    entry[1] = (unsigned char)old_state;
    entry[2] = (unsigned char)event;
    entry[3] = (unsigned char)new_state;
    rb_write(&g_log_rb, entry, 4U);
}

static const SM_Transition_t g_sm_table[MAX_MODES][MAX_STATES][MAX_EVENTS] = {
    /* MODE_NORMAL */
    {
        /* STATE_IDLE: START->ACTIVE, STOP->IDLE, PAUSE->IDLE, RESUME->IDLE */
        {
            { STATE_ACTIVE,    NULL },
            { STATE_IDLE,      NULL },
            { STATE_IDLE,      NULL },
            { STATE_IDLE,      NULL }
        },
        /* STATE_ACTIVE: START->ACTIVE, STOP->IDLE, PAUSE->SUSPENDED, RESUME->ACTIVE */
        {
            { STATE_ACTIVE,    NULL },
            { STATE_IDLE,      NULL },
            { STATE_SUSPENDED, NULL },
            { STATE_ACTIVE,    NULL }
        },
        /* STATE_SUSPENDED: START->SUSPENDED, STOP->IDLE, PAUSE->SUSPENDED, RESUME->ACTIVE */
        {
            { STATE_SUSPENDED, NULL },
            { STATE_IDLE,      NULL },
            { STATE_SUSPENDED, NULL },
            { STATE_ACTIVE,    NULL }
        }
    },
    /* MODE_DIAGNOSTIC */
    {
        /* STATE_IDLE */
        {
            { STATE_ACTIVE,    NULL },
            { STATE_IDLE,      NULL },
            { STATE_IDLE,      NULL },
            { STATE_IDLE,      NULL }
        },
        /* STATE_ACTIVE */
        {
            { STATE_ACTIVE,    NULL },
            { STATE_IDLE,      NULL },
            { STATE_SUSPENDED, NULL },
            { STATE_ACTIVE,    NULL }
        },
        /* STATE_SUSPENDED */
        {
            { STATE_SUSPENDED, NULL },
            { STATE_IDLE,      NULL },
            { STATE_SUSPENDED, NULL },
            { STATE_ACTIVE,    NULL }
        }
    }
};

static uint32_t get_hb_period(void)
{
    return (SM_getCurrentMode(&g_sm) == MODE_DIAGNOSTIC) ? 1U : 3U;
}

/* --------------------------------------------------------------------------
 * 6. SFS 協調タスク群
 * -------------------------------------------------------------------------- */
static void task_cmd_poll(void)
{
    uint32_t cmd = REG_CMD;
    if (cmd == 0U) {
        return;
    }

    __sync_synchronize();
    uint32_t data_in = REG_DATA_IN;

    switch (cmd) {
        case CMD_SM_START:
            SM_dispatch(&g_sm, EVENT_START, NULL);
            break;
        case CMD_SM_PAUSE:
            SM_dispatch(&g_sm, EVENT_PAUSE, NULL);
            break;
        case CMD_SM_RESUME:
            SM_dispatch(&g_sm, EVENT_RESUME, NULL);
            break;
        case CMD_SM_STOP:
            SM_dispatch(&g_sm, EVENT_STOP, NULL);
            break;
        case CMD_SET_MODE:
            if (data_in < MAX_MODES) {
                SM_setMode(&g_sm, (uint8_t)data_in);
                FRCGapCheckStart(&g_hb_frc, get_hb_period());
            }
            break;
        case CMD_ENQUEUE_DATA: {
            long item = (long)data_in;
            if (FIFO_is_full(&g_cmd_fifo)) {
                REG_STATUS |= STATUS_FIFO_OVERFLOW;
            } else {
                FIFO_push(&g_cmd_fifo, &item);
                REG_STATUS &= ~STATUS_FIFO_OVERFLOW;
            }
            break;
        }
        case CMD_SET_TIMER_PERIOD:
            if (data_in > 0U) {
                REG_TIMER_PERIOD = data_in;
            }
            break;
        case CMD_SHUTDOWN:
            REG_TIMER_CTRL = 0U;
            REG_STATUS = STATUS_SHUTDOWN;
            g_shutdown_requested = 1U;
            __sync_synchronize();
            REG_CMD = 0U;
            SFS_kill();
            return;
        default:
            break;
    }

    REG_SM_MODE = (uint32_t)SM_getCurrentMode(&g_sm);
    REG_SM_STATE = (uint32_t)SM_getCurrentState(&g_sm);
    REG_FIFO_DEPTH = (uint32_t)g_cmd_fifo.count;
    REG_RB_USED_BYTES = (uint32_t)rb_get_used_space(&g_log_rb);

    __sync_synchronize();
    REG_CMD = 0U;
}

static void task_sm_worker(void)
{
    if (g_shutdown_requested) {
        SFS_kill();
        return;
    }

    if (SM_getCurrentState(&g_sm) != STATE_ACTIVE) {
        return;
    }

    if (!g_worker_busy) {
        if (!FIFO_is_empty(&g_cmd_fifo)) {
            long popped = 0;
            if (FIFO_pop(&g_cmd_fifo, &popped) == 0) {
                g_worker_val = popped;
                g_worker_busy = 1U;
                REG_FIFO_DEPTH = (uint32_t)g_cmd_fifo.count;
                REG_STATUS = (REG_STATUS & ~STATUS_READY) | STATUS_BUSY;
                FRCGapCheckStart(&g_work_frc, 2UL);
            }
        }
        return;
    }

    if (FRCGapCheck(&g_work_frc) == 0UL) {
        uint32_t in_val = (uint32_t)g_worker_val;
        uint32_t result = (SM_getCurrentMode(&g_sm) == MODE_DIAGNOSTIC)
                              ? (in_val * 10U)
                              : (in_val * 3U + 1U);

        REG_DATA_OUT = result;
        REG_FIFO_PROCESSED = REG_FIFO_PROCESSED + 1U;
        REG_STATUS = (REG_STATUS & ~STATUS_BUSY) | STATUS_READY;
        g_worker_busy = 0U;
    }
}

static void task_telemetry_hb(void)
{
    if (g_shutdown_requested) {
        SFS_kill();
        return;
    }

    unsigned long now_ticks = GetFreeRunCounter();
    REG_FRCC_TICKS = (uint32_t)now_ticks;
    REG_FIFO_DEPTH = (uint32_t)g_cmd_fifo.count;
    REG_SM_MODE = (uint32_t)SM_getCurrentMode(&g_sm);
    REG_SM_STATE = (uint32_t)SM_getCurrentState(&g_sm);

    if (FRCGapCheck(&g_hb_frc) == 0UL) {
        REG_HEARTBEAT_CNT = REG_HEARTBEAT_CNT + 1U;
        FRCGapCheckStart(&g_hb_frc, get_hb_period());
    }

    /* Ring Buffer から 1 ティック間隔で 4 バイトずつドレインして Tracer 波形へ刻む */
    if (rb_get_used_space(&g_log_rb) >= 4U &&
        GetFreeRunGap(g_last_drain_tick, now_ticks) >= 1UL) {
        unsigned char entry[4];
        if (rb_read(&g_log_rb, entry, 4U) == 4U) {
            uint32_t packed = ((uint32_t)entry[0] << 24) |
                              ((uint32_t)entry[1] << 16) |
                              ((uint32_t)entry[2] << 8)  |
                              (uint32_t)entry[3];
            REG_LAST_TRANSITION = packed;
            REG_RB_DRAINED_CNT = REG_RB_DRAINED_CNT + 1U;
            g_last_drain_tick = now_ticks;
        }
    }

    REG_RB_USED_BYTES = (uint32_t)rb_get_used_space(&g_log_rb);
}

int main(void)
{
    REG_CMD             = 0U;
    REG_STATUS          = 0U;
    REG_DATA_IN         = 0U;
    REG_DATA_OUT        = 0U;
    REG_FRCC_TICKS      = 0U;
    REG_HEARTBEAT_CNT   = 0U;
    REG_SM_MODE         = 0U;
    REG_SM_STATE        = 0U;
    REG_FIFO_DEPTH      = 0U;
    REG_FIFO_PROCESSED  = 0U;
    REG_LAST_TRANSITION = 0U;
    REG_RB_USED_BYTES   = 0U;
    REG_RB_DRAINED_CNT  = 0U;

    gFreeRunCounter = 0UL;
    gFreeRunCounterMini = 0U;
    g_last_drain_tick = 0UL;

    FRCInterrupt(mcore_bsp_di, mcore_bsp_ei);
    mcore_bsp_register_timer_isr(on_timer_isr);

    FIFO_initialize(&g_cmd_fifo, g_fifo_buf, CMD_FIFO_CAPACITY, FIFO_TYPE_LONG);
    rb_init(&g_log_rb, g_rb_buf, LOG_RB_SIZE, RB_TRUE, NULL, NULL);
    SM_init(&g_sm,
            MODE_NORMAL,
            STATE_IDLE,
            MAX_STATES,
            MAX_EVENTS,
            &g_sm_table[0][0][0],
            on_sm_transition,
            NULL);

    FRCGapCheckStart(&g_hb_frc, get_hb_period());

    SFS_initialize();
    SFS_fork("CMD_POLL", 1, task_cmd_poll);
    SFS_fork("SM_WORKER", 2, task_sm_worker);
    SFS_fork("TELEM_HB", 3, task_telemetry_hb);

    __sync_synchronize();
    REG_TIMER_PERIOD = 500U;
    __sync_synchronize();
    REG_TIMER_CTRL = 1U;
    __sync_synchronize();
    REG_STATUS = STATUS_RUNNING;

    while (SFS_dispatch()) {
        mcore_bsp_idle_hint();
    }

    REG_TIMER_CTRL = 0U;
    return 0;
}
