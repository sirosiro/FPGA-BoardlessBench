#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// FPGA制御レジスタの物理メモリアドレスに対するポインタ定義（volatile修飾によりコンパイラ最適化を防止）
#define REG_CMD      (*(volatile uint32_t*)(uintptr_t)(0x40000010)) // Aコアからのコマンド指示レジスタ
#define REG_STATUS   (*(volatile uint32_t*)(uintptr_t)(0x40000014)) // Mコアの処理状態通知レジスタ
#define REG_DATA_IN  (*(volatile uint32_t*)(uintptr_t)(0x40000018)) // Aコアからの入力データレジスタ
#define REG_DATA_OUT (*(volatile uint32_t*)(uintptr_t)(0x4000001c)) // Mコアからの出力結果レジスタ

// タスク間通信用のFreeRTOSキューハンドル
static QueueHandle_t xQueue = NULL;

/* タスク1: FPGAのコマンドレジスタをポーリング監視し、要求をデータ処理タスクへ転送する */
void vTaskFPGAController(void *pvParameters) {
    (void)pvParameters;
    
    printf("[M-Core] FPGA Controller task started.\n");
    fflush(stdout);

    while (1) {
        // Aコアからのコマンド書き込みを監視（ポーリング）
        uint32_t cmd = REG_CMD;
        if (cmd == 0xA1) { // コマンド 0xA1（処理要求）を検出した場合
            uint32_t data = REG_DATA_IN; // 入力データを取得
            printf("[M-Core] Command 0xA1 detected. Data input: %u. Sending to processing task...\n", data);
            fflush(stdout);

            // 取得したデータを処理タスクへキュー経由で転送
            if (xQueueSend(xQueue, &data, 0) != pdPASS) {
                printf("[M-Core] Error: Failed to send data to queue.\n");
                fflush(stdout);
            }

            // コマンド受付完了をAコアへ知らせるため、レジスタをクリア
            REG_CMD = 0;
        }

        // シミュレーション時のCPU負荷を抑えるため、10msのディレイを挿入して処理権を譲る
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* タスク2: キューから要求を受信し、データ処理を実行して結果をFPGAレジスタに出力する */
void vTaskDataProcessor(void *pvParameters) {
    (void)pvParameters;
    uint32_t input_val = 0;

    printf("[M-Core] Data Processor task started.\n");
    fflush(stdout);

    while (1) {
        // キューにデータが入るまで無期限（portMAX_DELAY）でブロック待機
        if (xQueueReceive(xQueue, &input_val, portMAX_DELAY) == pdPASS) {
            printf("[M-Core] Processing data: %u...\n", input_val);
            fflush(stdout);

            // 実機の演算負荷による遅延を模擬するため、100ms待機
            vTaskDelay(pdMS_TO_TICKS(100));

            // データ処理の実行（例：入力データを2倍にする）
            uint32_t result = input_val * 2;

            // 演算結果を出力レジスタへ書き込み
            REG_DATA_OUT = result;

            // 処理完了（READY）状態を示すステータスフラグ（0x01）を書き込み
            REG_STATUS = 0x01;

            printf("[M-Core] Data processed. Result %u written to REG_DATA_OUT.\n", result);
            fflush(stdout);
        }
    }
}

int main(void) {
    printf("[M-Core] FreeRTOS firmware starting...\n");
    fflush(stdout);

    // 最大10個の uint32_t 型データを保持できるキューを作成
    xQueue = xQueueCreate(10, sizeof(uint32_t));
    if (xQueue == NULL) {
        printf("[M-Core] Error: Failed to create queue.\n");
        fflush(stdout);
        return 1;
    }

    // FPGA制御タスク（優先度1）とデータ処理タスク（優先度2）を作成
    xTaskCreate(vTaskFPGAController, "FPGA_Ctrl", 4096, NULL, 1, NULL);
    xTaskCreate(vTaskDataProcessor, "Data_Proc", 4096, NULL, 2, NULL);

    // FreeRTOSのスケジューラを開始（タスクの並行処理が開始される）
    vTaskStartScheduler();

    // 通常はここには到達しない
    for (;;);
    return 0;
}
