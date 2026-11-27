/**
 * @file bsp_sim_irq.c
 * @intent:responsibility F-BB シミュレーション環境向けの Mコア割り込み BSP 実装（SIGUSR1 + sigprocmask）。
 * @intent:rationale      RTL の mcore_irq_out 立ち上がりエッジから通知される SIGUSR1 を sigaction で捕捉し、
 *                        di/ei を sigprocmask によるペンディング保持型割り込みマスクとして再現する。
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mcore_irq_bsp.h"
#include <signal.h>
#include <stddef.h>
#include <unistd.h>

static void (*g_timer_isr)(void) = NULL;
static sigset_t g_irq_mask;
static volatile sig_atomic_t g_irq_nest = 0;

static void sigusr1_handler(int sig)
{
    (void)sig;
    if (g_timer_isr != NULL) {
        g_timer_isr();
    }
}

void mcore_bsp_register_timer_isr(void (*isr)(void))
{
    struct sigaction sa;

    g_timer_isr = isr;
    sigemptyset(&g_irq_mask);
    sigaddset(&g_irq_mask, SIGUSR1);

    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);
}

void mcore_bsp_di(void)
{
    if (g_irq_nest == 0) {
        sigprocmask(SIG_BLOCK, &g_irq_mask, NULL);
    }
    g_irq_nest++;
}

void mcore_bsp_ei(void)
{
    if (g_irq_nest > 0) {
        g_irq_nest--;
        if (g_irq_nest == 0) {
            sigprocmask(SIG_UNBLOCK, &g_irq_mask, NULL);
        }
    }
}

void mcore_bsp_idle_hint(void)
{
    usleep(1000);
}
