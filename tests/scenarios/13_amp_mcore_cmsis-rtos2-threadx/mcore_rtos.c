#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include "cmsis_os2.h"

uint32_t SystemCoreClock = 1000000U;

#define REG_CMD      (*(volatile uint32_t*)(uintptr_t)(0x40000010))
#define REG_STATUS   (*(volatile uint32_t*)(uintptr_t)(0x40000014))
#define REG_DATA_IN  (*(volatile uint32_t*)(uintptr_t)(0x40000018))
#define REG_DATA_OUT (*(volatile uint32_t*)(uintptr_t)(0x4000001c))

static osMessageQueueId_t xQueue = NULL;

void vTaskFPGAController(void *argument) {
    (void)argument;
    printf("[M-Core] FPGA Controller thread started.\n");
    fflush(stdout);

    while (1) {
        uint32_t cmd = REG_CMD;
        if (cmd == 0xA1) {
            uint32_t data = REG_DATA_IN;
            printf("[M-Core] Command 0xA1 detected via CMSIS-RTOS2 thread. Data input: %u. Sending to processing queue...\n", data);
            fflush(stdout);

            if (osMessageQueuePut(xQueue, &data, 0U, 0U) != osOK) {
                printf("[M-Core] Error: Failed to send data to queue.\n");
                fflush(stdout);
            }
            REG_CMD = 0;
        }
        osDelay(1U); // 1 tick = 10ms (100Hz)
    }
}

void vTaskDataProcessor(void *argument) {
    (void)argument;
    uint32_t input_val = 0;

    printf("[M-Core] Data Processor thread started.\n");
    fflush(stdout);

    while (1) {
        if (osMessageQueueGet(xQueue, &input_val, NULL, osWaitForever) == osOK) {
            printf("[M-Core] Processing data: %u...\n", input_val);
            fflush(stdout);

            osDelay(10U); // 10 ticks = 100ms (100Hz)

            uint32_t result = input_val * 2;
            REG_DATA_OUT = result;
            REG_STATUS = 0x01;

            printf("[M-Core] Data processed. Result %u written to REG_DATA_OUT.\n", result);
            fflush(stdout);
        }
    }
}

int main(void) {
    printf("[M-Core] CMSIS-RTOS2 firmware starting...\n");
    fflush(stdout);

    osStatus_t status = osKernelInitialize();
    if (status != osOK) {
        printf("[M-Core] Error: osKernelInitialize failed with status %d\n", (int)status);
        fflush(stdout);
        return 1;
    }

    xQueue = osMessageQueueNew(10U, sizeof(uint32_t), NULL);
    if (xQueue == NULL) {
        printf("[M-Core] Error: Failed to create queue.\n");
        fflush(stdout);
        return 1;
    }

    osThreadAttr_t attr1 = {0};
    attr1.name = "FPGA_Ctrl";
    attr1.stack_size = 4096;
    attr1.priority = osPriorityNormal;

    osThreadAttr_t attr2 = {0};
    attr2.name = "Data_Proc";
    attr2.stack_size = 4096;
    attr2.priority = osPriorityNormal1;

    osThreadId_t thread1 = osThreadNew(vTaskFPGAController, NULL, &attr1);
    if (thread1 == NULL) {
        printf("[M-Core] Error: Failed to create FPGA_Ctrl thread.\n");
        fflush(stdout);
        return 1;
    }

    osThreadId_t thread2 = osThreadNew(vTaskDataProcessor, NULL, &attr2);
    if (thread2 == NULL) {
        printf("[M-Core] Error: Failed to create Data_Proc thread.\n");
        fflush(stdout);
        return 1;
    }

    status = osKernelStart();
    if (status != osOK) {
        printf("[M-Core] Error: osKernelStart failed with status %d\n", (int)status);
        fflush(stdout);
        return 1;
    }

    for (;;);
    return 0;
}

/* OS Tick Stub implementation to satisfy CMSIS-RTOS2 Linker Requirements */
#include "os_tick.h"

int32_t OS_Tick_Setup (uint32_t freq, IRQHandler_t handler) {
    (void)freq;
    (void)handler;
    return 0;
}

void OS_Tick_Enable (void) {}
void OS_Tick_Disable (void) {}
void OS_Tick_AcknowledgeIRQ (void) {}

int32_t OS_Tick_GetIRQn (void) {
    return -1;
}

uint32_t OS_Tick_GetClock (void) {
    return 1000000U;
}

uint32_t OS_Tick_GetInterval (void) {
    return 10000U;
}

uint32_t OS_Tick_GetCount (void) {
    return 0U;
}

uint32_t OS_Tick_GetOverflow (void) {
    return 0U;
}

