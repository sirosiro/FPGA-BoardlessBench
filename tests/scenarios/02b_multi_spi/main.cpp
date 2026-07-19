#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cstring>
#include <cassert>
#include <vector>

#include "vfpga_device_config.h"
// テスト用マクロ
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] ASSERTION FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

/**
 * @brief SPIデバイスへ1回の全二重転送要求を発行するヘルパ
 */
bool spi_transfer(int fd, const uint8_t* tx, uint8_t* rx, size_t len) {
    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = len;
    tr.speed_hz = 1000000;
    tr.bits_per_word = 8;
    tr.delay_usecs = 0;

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        std::perror("[SPI Test] ioctl SPI_IOC_MESSAGE failed");
        return false;
    }
    return true;
}

int main() {
    std::cout << "[SPI Test] Starting SPI multi-device verification...\n";

    // =========================================================================
    // 1. SPI Flash (W25Q128) の検証
    // =========================================================================
    std::cout << "[SPI Test] 1. Opening SPI Flash (" FBB_DEV_PATH_SPI ".0)...\n";
    int flash_fd = open(FBB_DEV_PATH_SPI ".0", O_RDWR);
    TEST_ASSERT(flash_fd >= 0, "Failed to open " FBB_DEV_PATH_SPI ".0");

    // 1.1 JEDEC IDの読み出し
    std::cout << "[SPI Test] Read JEDEC ID...\n";
    uint8_t tx_id[4] = {0x9F, 0x00, 0x00, 0x00};
    uint8_t rx_id[4] = {0};
    bool ok = spi_transfer(flash_fd, tx_id, rx_id, 4);
    TEST_ASSERT(ok, "JEDEC ID transfer failed");

    std::printf("[SPI Test] JEDEC ID: Manuf=0x%02X, Type=0x%02X, Capacity=0x%02X\n", rx_id[1], rx_id[2], rx_id[3]);
    TEST_ASSERT(rx_id[1] == 0xEF, "Incorrect Manufacturer ID (expected 0xEF)");
    TEST_ASSERT(rx_id[2] == 0x40, "Incorrect Memory Type (expected 0x40)");
    TEST_ASSERT(rx_id[3] == 0x18, "Incorrect Capacity ID (expected 0x18)");

    // 1.2 Status Register WELビットのリセット確認
    uint8_t tx_status[2] = {0x05, 0x00};
    uint8_t rx_status[2] = {0};
    spi_transfer(flash_fd, tx_status, rx_status, 2);
    std::printf("[SPI Test] Status Reg 1 (Before WEL): 0x%02X\n", rx_status[1]);

    // 1.3 Write Enable 送信
    uint8_t tx_wel[1] = {0x06};
    uint8_t rx_wel[1] = {0};
    spi_transfer(flash_fd, tx_wel, rx_wel, 1);

    // 1.4 WELビットの確認 (WELビット = 0x02)
    spi_transfer(flash_fd, tx_status, rx_status, 2);
    std::printf("[SPI Test] Status Reg 1 (After WEL): 0x%02X\n", rx_status[1]);
    TEST_ASSERT((rx_status[1] & 0x02) != 0, "WEL bit was not set");

    // 1.5 セクタ消去 (Sector Erase - 4KB @ Addr 0x001000)
    std::cout << "[SPI Test] Erasing sector at 0x001000...\n";
    uint8_t tx_erase[4] = {0x20, 0x00, 0x10, 0x00};
    uint8_t rx_erase[4] = {0};
    spi_transfer(flash_fd, tx_erase, rx_erase, 4);

    // 1.6 消去されたことの確認 (0xFFが返るはず)
    uint8_t tx_read_empty[8] = {0x03, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t rx_read_empty[8] = {0};
    spi_transfer(flash_fd, tx_read_empty, rx_read_empty, 8);
    for (int i = 4; i < 8; ++i) {
        TEST_ASSERT(rx_read_empty[i] == 0xFF, "Sector Erase failed (not 0xFF)");
    }

    // 1.7 ページプログラム (Page Program - Write data @ Addr 0x001000)
    std::cout << "[SPI Test] Page Program at 0x001000...\n";
    // Write Enable (毎回必要)
    spi_transfer(flash_fd, tx_wel, rx_wel, 1);

    uint8_t tx_write[8] = {0x02, 0x00, 0x10, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rx_write[8] = {0};
    spi_transfer(flash_fd, tx_write, rx_write, 8);

    // 1.8 書き込みデータの読み出し確認
    std::cout << "[SPI Test] Reading back written data...\n";
    uint8_t tx_read[8] = {0x03, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t rx_read[8] = {0};
    spi_transfer(flash_fd, tx_read, rx_read, 8);

    std::printf("[SPI Test] Read Back: 0x%02X 0x%02X 0x%02X 0x%02X\n", rx_read[4], rx_read[5], rx_read[6], rx_read[7]);
    TEST_ASSERT(rx_read[4] == 0xDE, "Verify failed (Byte 0)");
    TEST_ASSERT(rx_read[5] == 0xAD, "Verify failed (Byte 1)");
    TEST_ASSERT(rx_read[6] == 0xBE, "Verify failed (Byte 2)");
    TEST_ASSERT(rx_read[7] == 0xEF, "Verify failed (Byte 3)");

    close(flash_fd);
    std::cout << "[SPI Test] SPI Flash tests passed.\n";

    // =========================================================================
    // 2. SPI ADC (MCP3208) の検証
    // =========================================================================
    std::cout << "[SPI Test] 2. Opening SPI ADC (" FBB_DEV_PATH_SPI ".1)...\n";
    int adc_fd = open(FBB_DEV_PATH_SPI ".1", O_RDWR);
    TEST_ASSERT(adc_fd >= 0, "Failed to open " FBB_DEV_PATH_SPI ".1");

    // 2.1 CH0のADC値読み出し
    // MCP3208プロトコル:
    // tx = [Start bit (0x01), Single-Ended CH0 (0x80), Dummy (0x00)]
    uint8_t tx_adc[3] = {0x01, 0x80, 0x00};
    uint8_t rx_adc[3] = {0};
    ok = spi_transfer(adc_fd, tx_adc, rx_adc, 3);
    TEST_ASSERT(ok, "ADC read failed");

    // rx_adc[1] の下位4ビットに B11-B8, rx_adc[2] に B7-B0 が含まれる
    uint16_t adc_val = ((rx_adc[1] & 0x0F) << 8) | rx_adc[2];
    std::printf("[SPI Test] ADC Channel 0 Raw Digital Value: %d (expected %d)\n", adc_val, 2048);

    // DTSで初期設定した値 2048 と一致するか検証
    TEST_ASSERT(adc_val == 2048, "ADC read value mismatch with DTS fbb,mock-data");

    // VFPGA_INTERACTIVE 環境変数が有効な場合、8チャンネルのADC値を繰り返し監視し、UART(/dev/ttyPS1)に出力する
    if (getenv("VFPGA_INTERACTIVE") != nullptr) {
        int uart_fd = open(FBB_DEV_PATH_SERIAL, O_RDWR);
        
        // 仮想 FPGA レジスタへのマッピング (/dev/uio0)
        int uio_fd = open(FBB_DEV_PATH_UIO, O_RDWR);
        volatile uint32_t* uio_regs = nullptr;
        if (uio_fd >= 0) {
            uio_regs = (volatile uint32_t*)mmap(NULL, 1024, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);
            if (uio_regs == MAP_FAILED) {
                uio_regs = nullptr;
            }
        }

        if (uart_fd >= 0) {
            const char* msg_init = "\r\n[SPI Test] ==============================================\r\n"
                                   "[SPI Test] Interactive Mode: Monitoring CH0-CH7 via UART.\r\n"
                                   "[SPI Test] Slide any channel in the Dashboard to see changes!\r\n"
                                   "[SPI Test] (ADC data will also flow into virtual FPGA registers)\r\n"
                                   "[SPI Test] ==============================================\r\n\r\n";
            write(uart_fd, msg_init, strlen(msg_init));

            uint16_t last_vals[8];
            for (int ch = 0; ch < 8; ch++) {
                last_vals[ch] = 9999;
                if (uio_regs != nullptr) {
                    uio_regs[ch] = 2048; // 初期値
                }
            }

            while (true) {
                for (int ch = 0; ch < 8; ch++) {
                    uint8_t tx_loop[3];
                    tx_loop[0] = 0x01; // Start bit
                    tx_loop[1] = 0x80 | (ch << 4); // Single-Ended + CH select
                    tx_loop[2] = 0x00;
                    uint8_t rx_loop[3] = {0};

                    if (spi_transfer(adc_fd, tx_loop, rx_loop, 3)) {
                        uint16_t val = ((rx_loop[1] & 0x0F) << 8) | rx_loop[2];
                        if (last_vals[ch] == 9999) {
                            last_vals[ch] = val;
                        } else if (val != last_vals[ch]) {
                            double voltage = (val / 4095.0) * 3.3;
                            char log_buf[128];
                            snprintf(log_buf, sizeof(log_buf), 
                                     "[SPI Monitor] CH%d changed: %4d LSB (%.2f V)\r\n", 
                                     ch, val, voltage);
                            write(uart_fd, log_buf, strlen(log_buf));
                            
                            // 仮想 FPGA のレジスタに流し込む (REG_ADC_CH0〜CH7)
                            if (uio_regs != nullptr) {
                                uio_regs[ch] = val;
                            }
                            
                            last_vals[ch] = val;
                        }
                    }
                }
                usleep(50000); // 50ms
            }
            
            if (uio_regs != nullptr) {
                munmap((void*)uio_regs, 1024);
            }
            if (uio_fd >= 0) {
                close(uio_fd);
            }
            close(uart_fd);
        } else {
            std::cerr << "[Warning] Failed to open /dev/ttyPS1 for interactive output.\n";
            if (uio_regs != nullptr) {
                munmap((void*)uio_regs, 1024);
            }
            if (uio_fd >= 0) {
                close(uio_fd);
            }
        }
    }

    close(adc_fd);
    std::cout << "[SPI Test] SPI ADC tests passed.\n";

    std::cout << "[SPI Test] ALL SPI TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
