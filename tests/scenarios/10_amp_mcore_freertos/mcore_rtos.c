#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define REG_CMD      (*(volatile uint32_t*)(0x40000010))
#define REG_STATUS   (*(volatile uint32_t*)(0x40000014))
#define REG_DATA_IN  (*(volatile uint32_t*)(0x40000018))
#define REG_DATA_OUT (*(volatile uint32_t*)(0x4000001c))

static QueueHandle_t xQueue = NULL;

/* Task 1: Polls FPGA command register and forwards requests to data processing task */
void vTaskFPGAController(void *pvParameters) {
    (void)pvParameters;
    
    printf("[M-Core] FPGA Controller task started.\n");
    fflush(stdout);

    while (1) {
        // Poll for command from A-Core
        uint32_t cmd = REG_CMD;
        if (cmd == 0xA1) {
            uint32_t data = REG_DATA_IN;
            printf("[M-Core] Command 0xA1 detected. Data input: %u. Sending to processing task...\n", data);
            fflush(stdout);

            // Forward the data to the processing task
            if (xQueueSend(xQueue, &data, 0) != pdPASS) {
                printf("[M-Core] Error: Failed to send data to queue.\n");
                fflush(stdout);
            }

            // Clear the command to acknowledge receipt
            REG_CMD = 0;
        }

        // Delay task for 10ms to prevent CPU hogging in simulation
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* Task 2: Receives data from queue, processes it, and writes results to FPGA */
void vTaskDataProcessor(void *pvParameters) {
    (void)pvParameters;
    uint32_t input_val = 0;

    printf("[M-Core] Data Processor task started.\n");
    fflush(stdout);

    while (1) {
        // Block indefinitely until a queue item is received
        if (xQueueReceive(xQueue, &input_val, portMAX_DELAY) == pdPASS) {
            printf("[M-Core] Processing data: %u...\n", input_val);
            fflush(stdout);

            // Simulate computation delay
            vTaskDelay(pdMS_TO_TICKS(100));

            // Process data: Multiply by 2
            uint32_t result = input_val * 2;

            // Write output register
            REG_DATA_OUT = result;

            // Signal READY status to A-Core
            REG_STATUS = 0x01;

            printf("[M-Core] Data processed. Result %u written to REG_DATA_OUT.\n", result);
            fflush(stdout);
        }
    }
}

int main(void) {
    printf("[M-Core] FreeRTOS firmware starting...\n");
    fflush(stdout);

    // Create a queue to hold 10 uint32_t values
    xQueue = xQueueCreate(10, sizeof(uint32_t));
    if (xQueue == NULL) {
        printf("[M-Core] Error: Failed to create queue.\n");
        fflush(stdout);
        return 1;
    }

    // Create tasks
    xTaskCreate(vTaskFPGAController, "FPGA_Ctrl", 4096, NULL, 1, NULL);
    xTaskCreate(vTaskDataProcessor, "Data_Proc", 4096, NULL, 2, NULL);

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should not reach here
    for (;;);
    return 0;
}
