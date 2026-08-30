/**
 * @file main.c
 * @intent:responsibility F-BB 最小構成テンプレート（1 レジスタ、1 UIO）の基本ビルドと実行を検証する。
 * @intent:rationale      新規シナリオ作成時のスケルトンコードの完全性をテストする。
 * @intent:pre-condition  最小限の config.dts がロードされていること。
 */

#include <stdio.h>
#include <unistd.h>

/**
 * Minimal Firmware for Minimum Template
 * 
 * このプログラムは何もせず、シミュレーションが走る時間を稼ぐだけです。
 * シミュレーション中に生成される vfpga.vcd を確認してください。
 */
int main() {
    printf("[Minimum Template] Starting minimal simulation...\n");
    printf("[Minimum Template] Simulation is running in the background.\n");
    printf("[Minimum Template] Waiting 5 seconds for waveform generation...\n");
    
    sleep(5);
    
    printf("[Minimum Template] Done. Check vfpga.vcd using GTKWave.\n");
    return 0;
}
