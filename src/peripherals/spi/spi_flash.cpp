#include "../common/spi_slave.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <csignal>

constexpr size_t FLASH_SIZE_16MB = 16 * 1024 * 1024; // 16MB (W25Q128)
constexpr size_t SECTOR_SIZE_4KB = 4 * 1024;         // 4KB セクタ

class SpiFlash : public SpiSlave {
public:
    /**
     * @brief SPI Flashエミュレータコンストラクタ
     * @param cs チップセレクト番号
     * @param mock_file 不揮発状態を保存するファイルパス (空の場合は不揮発保存を行わない)
     */
    SpiFlash(uint8_t cs, const std::string& mock_file)
        : SpiSlave(cs), 
          mock_file_(mock_file),
          memory_(FLASH_SIZE_16MB, 0xFF) // フラッシュは消去状態で 0xFF
    {
        // 不揮発ファイルが存在する場合は状態をロード
        if (!mock_file_.empty()) {
            std::ifstream f(mock_file_, std::ios::binary);
            if (f.is_open()) {
                f.read(reinterpret_cast<char*>(memory_.data()), memory_.size());
                std::cout << "[SPI Flash] Loaded " << f.gcount() << " bytes from " << mock_file_ << "\n";
            }
        }
    }

protected:
    /**
     * @brief SPI全二重転送のシミュレーション
     * コマンドの1バイト目を読み取り、W25Q128のコマンド体系を模倣します。
     */
    std::vector<uint8_t> onTransfer(const std::vector<uint8_t>& tx_data) override {
        std::vector<uint8_t> rx_data(tx_data.size(), 0);
        if (tx_data.empty()) return rx_data;

        uint8_t cmd = tx_data[0];

        switch (cmd) {
            case 0x9F: // Read JEDEC ID (Winbond Manufacturer ID: 0xEF, Device ID: 0x4018)
                if (tx_data.size() > 1) rx_data[1] = 0xEF; // Manufacturer ID
                if (tx_data.size() > 2) rx_data[2] = 0x40; // Memory Type
                if (tx_data.size() > 3) rx_data[3] = 0x18; // Capacity ID (W25Q128)
                break;

            case 0x05: // Read Status Register 1
                if (tx_data.size() > 1) {
                    rx_data[1] = status_reg1_;
                }
                break;

            case 0x06: // Write Enable
                status_reg1_ |= 0x02; // WEL (Write Enable Latch) ビットをセット
                break;

            case 0x04: // Write Disable
                status_reg1_ &= ~0x02; // WEL ビットをクリア
                break;

            case 0x03: // Read Data (0x03 + Addr[23:16] + Addr[15:8] + Addr[7:0] + Data...)
                if (tx_data.size() >= 4) {
                    uint32_t addr = (tx_data[1] << 16) | (tx_data[2] << 8) | tx_data[3];
                    for (size_t i = 4; i < tx_data.size(); ++i) {
                        uint32_t target_addr = (addr + (i - 4)) % FLASH_SIZE_16MB;
                        rx_data[i] = memory_[target_addr];
                    }
                }
                break;

            case 0x02: // Page Program (0x02 + Addr[23:16] + Addr[15:8] + Addr[7:0] + Data...)
                if (tx_data.size() >= 4 && (status_reg1_ & 0x02)) {
                    uint32_t addr = (tx_data[1] << 16) | (tx_data[2] << 8) | tx_data[3];
                    for (size_t i = 4; i < tx_data.size(); ++i) {
                        uint32_t target_addr = (addr + (i - 4)) % FLASH_SIZE_16MB;
                        // Flashは0への書き込みのみ可能 (消去状態でなければ上書きできないが、エミュレートでは単純にAND書き込み)
                        memory_[target_addr] &= tx_data[i];
                    }
                    saveToFile();
                    status_reg1_ &= ~0x02; // WELビットクリア
                }
                break;

            case 0x20: // Sector Erase (4KB) (0x20 + Addr[23:16] + Addr[15:8] + Addr[7:0])
                if (tx_data.size() >= 4 && (status_reg1_ & 0x02)) {
                    uint32_t addr = (tx_data[1] << 16) | (tx_data[2] << 8) | tx_data[3];
                    uint32_t sector_start = (addr / SECTOR_SIZE_4KB) * SECTOR_SIZE_4KB;
                    
                    if (sector_start + SECTOR_SIZE_4KB <= FLASH_SIZE_16MB) {
                        std::fill(memory_.begin() + sector_start, memory_.begin() + sector_start + SECTOR_SIZE_4KB, 0xFF);
                        saveToFile();
                    }
                    status_reg1_ &= ~0x02; // WELビットクリア
                }
                break;

            default:
                // 未対応のコマンドは無視
                break;
        }

        return rx_data;
    }

private:
    /**
     * @brief メモリの状態をファイルに書き出す
     */
    void saveToFile() {
        if (mock_file_.empty()) return;
        std::ofstream f(mock_file_, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(memory_.data()), memory_.size());
        }
    }

    std::string mock_file_;            ///< 不揮発保存ファイル名
    std::vector<uint8_t> memory_;      ///< 16MBのフラッシュメモリ領域
    uint8_t status_reg1_{0x00};        ///< ステータスレジスタ1
};

// グローバルスコープのシグナル制御用インスタンスポインタ
static SpiFlash* g_flash_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_flash_instance) {
        std::cout << "\n[SPI Flash] Stopping daemon safely...\n";
        g_flash_instance->stop();
    }
}

int main(int argc, char *argv[]) {
    std::string sock_file;
    std::string mock_file;

    // コマンドライン引数解析
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            sock_file = argv[++i];
        } else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            mock_file = argv[++i];
        }
    }

    if (sock_file.empty()) {
        std::cerr << "Usage: " << argv[0] << " --socket <socket_path> [--file <mock_file>]\n"
                  << "Options:\n"
                  << "  --socket    UNIX domain socket path (Required)\n"
                  << "  --file      Persistence backing file path for flash memory (Optional)\n";
        return 1;
    }

    // デバイスエミュレータ起動
    SpiFlash flash(0, mock_file);
    g_flash_instance = &flash;

    // クリーンアップ用のシグナルハンドリング
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "[SPI Flash] Mock daemon starting on " << sock_file << "\n";
    std::flush(std::cout);

    // エミュレーション稼働 (接続待ちループ、ブロッキング)
    if (!flash.start(sock_file)) {
        std::cerr << "[SPI Flash] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[SPI Flash] Daemon stopped.\n";
    return 0;
}
