/**
 * @file src/peripherals/official_plugins/winbond_w25q128/spi_flash.cpp
 * @intent:responsibility
 *   Winbond W25Q128 16MB (128Mbit) SPI NOR Flash メモリのエミュレーションデーモン。
 *   JEDEC ID 読み出し、ステータスレジスタ、ライトイネーブル、データ読み出し、ページプログラム、
 *   および 4KB セクタ消去を完全エミュレートする。
 * @intent:rationale
 *   実機データシート準拠のコマンドセット（0x9F, 0x05, 0x06, 0x04, 0x03, 0x02, 0x20）を実装し、
 *   オプションのバッキングファイル（--file）によって電源切断（プロセス再起動）を跨ぐ不揮発ストレージの永続化を実現する。
 */

#include "../../common/spi_slave.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <csignal>

constexpr size_t FLASH_SIZE_16MB = 16 * 1024 * 1024; // 16MB (W25Q128)
constexpr size_t SECTOR_SIZE_4KB = 4 * 1024;         // 4KB セクタ

/**
 * @class SpiFlash
 * @intent:responsibility
 *   W25Q128 コマンドデコード、16MB メモリアレイ管理、Write Enable Latch（WEL）管理、ファイル永続化。
 */
class SpiFlash : public SpiSlave {
public:
    /**
     * @brief SPI Flashエミュレータコンストラクタ
     * @param cs チップセレクト番号
     * @param mock_file 不揮発状態を保存するファイルパス (空の場合は不揮発保存を行わない)
     * @intent:responsibility メモリを 0xFF (消去状態) で初期化し、mock_file が存在すればデータを復元する。
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
     * @param tx_data 先頭バイトが W25Q128 コマンド
     * @return 読み出しデータを含む同期応答
     * @intent:responsibility 0x9F(JEDEC ID), 0x05(Status), 0x06(WREN), 0x04(WRDI), 0x03(Read), 0x02(Program), 0x20(Erase) を処理。
     */
    std::vector<uint8_t> onTransfer(const std::vector<uint8_t>& tx_data) override {
        std::vector<uint8_t> rx_data(tx_data.size(), 0);
        if (tx_data.empty()) return rx_data;

        uint8_t cmd = tx_data[0];

        switch (cmd) {
            case 0x9F: // Read JEDEC ID (Winbond Manufacturer ID: 0xEF, Memory Type: 0x40, Capacity: 0x18)
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
     * @brief メモリの状態をファイルに書き出す。
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

#include "../../common/cli_helper.hpp"

// グローバルスコープのシグナル制御用インスタンスポインタ
static SpiFlash* g_flash_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 * @intent:responsibility SIGINT/SIGTERM を受信した際に SPI Flash デーモンを安全に停止し、メモリ内容をファイルへフラッシュする。
 * @intent:rationale ソケットの孤立を防ぎ、不揮発データの保存整合性を担保する。
 * @intent:pre-condition g_flash_instance が初期化されていること。
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_flash_instance) {
        std::cout << "\n[SPI Flash] Stopping daemon safely...\n";
        g_flash_instance->stop();
    }
}

/**
 * @brief W25Q128 SPI NOR Flash エミュレータデーモンエントリポイント
 * @intent:responsibility CLI 引数をパースして SPI ソケット上で Flash エミュレーションデーモンを起動する。
 * @intent:rationale 仮想 SPI バス経由で JEDEC ID 読み出しやセクタ消去・ページ書き込みコマンドに応答する。
 * @intent:pre-condition --socket オプションで有効なソケットパスが指定されていること。
 */
int main(int argc, char *argv[]) {
    fbb::PluginCLI cli("Winbond W25Q128 16MB SPI NOR Flash",
                       "Emulates 16MB SPI NOR Flash memory with optional non-volatile file persistence (--file).");
    auto opt = cli.parse(argc, argv);
    if (opt.show_help) return 0;

    if (opt.socket_path.empty()) {
        std::cerr << "Error: --socket <socket_path> is required.\n\n";
        cli.print_help();
        return 1;
    }

    // デバイスエミュレータ起動
    SpiFlash flash(0, opt.file_path);
    g_flash_instance = &flash;

    fbb::PluginCLI::setup_signal_handler(handle_signal);

    std::cout << "[SPI Flash] Mock daemon starting on " << opt.socket_path << "\n";
    std::flush(std::cout);

    // エミュレーション稼働 (接続待ちループ、ブロッキング)
    if (!flash.start(opt.socket_path)) {
        std::cerr << "[SPI Flash] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[SPI Flash] Daemon stopped.\n";
    return 0;
}
