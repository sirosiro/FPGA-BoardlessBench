#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "vfpga_device_config.h"
#define UIO_DEV FBB_DEV_PATH_UIO
#define UIO_SIZE 4096

// Registers Offset
#define REG_SPI_TX     0x00
#define REG_SPI_RX     0x04
#define REG_SPI_CTRL   0x08
#define REG_SPI_STATUS 0x0c

volatile uint32_t* regs = nullptr;

uint8_t spi_transfer_byte(uint8_t tx) {
    regs[REG_SPI_TX / 4] = tx;
    regs[REG_SPI_CTRL / 4] = 1;
    
    // Busy (Bit 0 of STATUS) が 1 に立ち上がるのを待つ
    int timeout = 0;
    while (!(regs[REG_SPI_STATUS / 4] & 0x1) && timeout < 1000) {
        usleep(1);
        timeout++;
    }

    // Busy が 0 に戻る (送信完了) のを待つ
    while (regs[REG_SPI_STATUS / 4] & 0x1) {
        usleep(1);
    }

    // RXレジスタから読み出し
    return regs[REG_SPI_RX / 4] & 0xFF;
}

int main() {
    printf("[FW] PL SPI verification firmware started.\n");

    int fd = open(UIO_DEV, O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/uio0");
        return 1;
    }

    regs = (volatile uint32_t*)mmap(NULL, UIO_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    // spi_adc.cpp (MCP3208) への問い合わせシーケンス (4バイト)
    // ソフトウェア遅延により1バイト遅れるため、4バイト送信して下位8ビットを完全に回収します。
    // Byte 1: StartBit => 0x01
    // Byte 2: SGL/DIFF=1, CH0 => 0x80
    // Byte 3: dummy => 0x00
    // Byte 4: dummy => 0x00
    
    uint8_t rx1 = spi_transfer_byte(0x01);
    uint8_t rx2 = spi_transfer_byte(0x80);
    uint8_t rx3 = spi_transfer_byte(0x00);
    uint8_t rx4 = spi_transfer_byte(0x00);

    // MCP3208の応答フォーマット（1バイト遅延考慮）：
    // rx3の下位4ビット(D11~D8)と、rx4の8ビット(D7~D0)を結合して12ビットにする
    uint16_t adc_val = ((rx3 & 0x0F) << 8) | rx4;

    printf("[FW] Received bytes: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", rx1, rx2, rx3, rx4);
    printf("[FW] Decoded ADC Value: %d (Expected: 2048)\n", adc_val);

    // アサーション検証
    assert(adc_val == 2048);

    printf("[FW] PL SPI ADC verification passed successfully.\n");

    // VFPGA_INTERACTIVE 環境変数が有効な場合、無限監視ループに入る
    if (getenv("VFPGA_INTERACTIVE") != nullptr) {
        int uart_fd = open(FBB_DEV_PATH_SERIAL, O_RDWR);
        if (uart_fd >= 0) {
            const char* msg_init = "\r\n[PL SPI Test] ==============================================\r\n"
                                   "[PL SPI Test] Interactive Mode: Monitoring CH0 via PL SPI.\r\n"
                                   "[PL SPI Test] Slide CH0 in the Dashboard to see changes!\r\n"
                                   "[PL SPI Test] ==============================================\r\n\r\n";
            write(uart_fd, msg_init, strlen(msg_init));
        }

        uint16_t last_val = 9999;
        while (true) {
            uint8_t rx1_loop = spi_transfer_byte(0x01);
            uint8_t rx2_loop = spi_transfer_byte(0x80);
            uint8_t rx3_loop = spi_transfer_byte(0x00);
            uint8_t rx4_loop = spi_transfer_byte(0x00);

            uint16_t val = ((rx3_loop & 0x0F) << 8) | rx4_loop;
            if (last_val == 9999) {
                last_val = val;
            } else if (val != last_val) {
                double voltage = (val / 4095.0) * 3.3;
                char log_buf[128];
                snprintf(log_buf, sizeof(log_buf), 
                         "[PL SPI Monitor] ADC CH0 changed: %4d LSB (%.2f V)\r\n", 
                         val, voltage);
                if (uart_fd >= 0) {
                    write(uart_fd, log_buf, strlen(log_buf));
                } else {
                    printf("%s", log_buf); fflush(stdout);
                }
                last_val = val;
            }
            usleep(200000); // 200ms周期でスキャン
        }
        if (uart_fd >= 0) close(uart_fd);
    }
    
    // clean up
    munmap((void*)regs, UIO_SIZE);
    close(fd);
    return 0;
}
