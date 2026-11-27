/**
 * @file mcore_irq_bsp.h
 * @intent:responsibility Mコアファームウェア向けの割り込み制御・省電力待機 BSP 抽象インターフェースを定義する。
 * @intent:rationale      実機 Zynq (XScuGic / __disable_irq) と F-BB シミュレーション (POSIX signal / sigprocmask) を
 *                        リンク時多態により差し替え、mcore_sfs.c の 100% 実機透過性を維持する。
 */
#ifndef MCORE_IRQ_BSP_H
#define MCORE_IRQ_BSP_H

void mcore_bsp_register_timer_isr(void (*isr)(void));
void mcore_bsp_di(void);
void mcore_bsp_ei(void);
void mcore_bsp_idle_hint(void);

#endif /* MCORE_IRQ_BSP_H */
