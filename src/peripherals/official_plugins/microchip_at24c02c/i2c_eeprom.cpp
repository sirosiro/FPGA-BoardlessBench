/**
 * @file src/peripherals/official_plugins/microchip_at24c02c/i2c_eeprom.cpp
 * @intent:responsibility
 *   Microchip AT24C02C 2Kbit (256 バイト) I2C シリアル EEPROM のエミュレーションデーモン。
 *   I2C 経由で受信したアドレスポインタ設定、シーケンシャル読み出し、バイト/ページ書き込みを処理する。
 * @intent:rationale
 *   256 バイトのメモリ空間に対するオートインクリメント読み書きと、オプションのバッキングファイル（--file）による
 *   電源遮断（再起動）を跨ぐ不揮発ストレージ永続化機能を提供する。
 */

#include "../../common/i2c_slave.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <csignal>

constexpr size_t EEPROM_SIZE = 256;

/**
 * @class I2cEeprom
 * @intent:responsibility
 *   256 バイト EEPROM アレイの管理、現在アドレスポインタの追跡、およびファイル同期。
 */
class I2cEeprom : public I2cSlave {
public:
    /**
     * @brief EEPROMエミュレータコンストラクタ
     * @param dev_addr I2Cデバイスアドレス（通常 0x50）
     * @param init_val メモリセルのデフォルト初期値
     * @param mock_file 不揮発状態を保存するファイルパス (空の場合は不揮発保存を行わない)
     * @intent:responsibility メモリを初期値で埋め、mock_file が存在すればデータを復元する。
     */
    I2cEeprom(uint8_t dev_addr, uint8_t init_val, const std::string& mock_file)
        : I2cSlave(dev_addr), 
          mock_file_(mock_file),
          memory_(EEPROM_SIZE, 0)
    {
        // 1. 初期値でメモリを埋める
        std::fill(memory_.begin(), memory_.end(), init_val);
        
        // 2. 不揮発ファイルが存在する場合は状態をロード
        if (!mock_file_.empty()) {
            std::ifstream f(mock_file_, std::ios::binary);
            if (f.is_open()) {
                f.read(reinterpret_cast<char*>(memory_.data()), memory_.size());
                std::cout << "[I2C EEPROM] Loaded " << f.gcount() << " bytes from " << mock_file_ << "\n";
            }
        }
    }

protected:
    /**
     * @brief I2C書き込み処理のシミュレーション
     * @param data 1バイト目: メモリアドレスポインタ, 2バイト目以降: 連続データ書き込み
     * @intent:responsibility アドレスポインタを更新し、続くペイロードバイトをオートインクリメントで書き込む。
     */
    void onWrite(const std::vector<uint8_t>& data) override {
        if (data.empty()) return;

        // 1バイト目は書き込み/読み出しの開始アドレス指定
        current_addr_ = data[0] % EEPROM_SIZE;

        // 2バイト目以降がある場合は、それをメモリに書き込む (オートインクリメント)
        if (data.size() > 1) {
            for (size_t i = 1; i < data.size(); ++i) {
                memory_[current_addr_] = data[i];
                current_addr_ = (current_addr_ + 1) % EEPROM_SIZE;
            }

            // 不揮発ファイルへ状態を即座に同期・保存
            saveToFile();
        }
    }

    /**
     * @brief I2C読み出し処理のシミュレーション
     * @param length 要求バイト数
     * @return 現在のアドレスポインタから読み出したバイト列
     * @intent:responsibility 現在のアドレスポインタから length バイトを返し、ポインタをオートインクリメントする。
     */
    std::vector<uint8_t> onRead(size_t length) override {
        std::vector<uint8_t> buf(length);
        for (size_t i = 0; i < length; ++i) {
            buf[i] = memory_[current_addr_];
            current_addr_ = (current_addr_ + 1) % EEPROM_SIZE;
        }
        return buf;
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
    std::vector<uint8_t> memory_;      ///< 256バイトのEEPROMメモリセル
    size_t current_addr_{0};           ///< 現在の読み書きアドレスポインタ
};

#include "../../common/cli_helper.hpp"

// グローバルスコープのシグナル制御用インスタンスポインタ
static I2cEeprom* g_eeprom_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 * @intent:responsibility SIGINT/SIGTERM を受信した際に I2C EEPROM デーモンを安全に停止し、メモリ内容をファイルへフラッシュする。
 * @intent:rationale ソケットの孤立を防ぎ、不揮発データの保存整合性を担保する。
 * @intent:pre-condition g_eeprom_instance が初期化されていること。
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_eeprom_instance) {
        std::cout << "\n[I2C EEPROM] Stopping daemon safely...\n";
        g_eeprom_instance->stop();
    }
}

/**
 * @brief AT24C02C I2C EEPROM エミュレータデーモンエントリポイント
 * @intent:responsibility CLI 引数をパースして I2C ソケット上で EEPROM エミュレーションデーモンを起動する。
 * @intent:rationale 仮想 I2C バス経由でバイト/ページ読み書き要求に応答し、ファイル永続化を提供する。
 * @intent:pre-condition --socket オプションで有効なソケットパスが指定されていること。
 */
int main(int argc, char *argv[]) {
    fbb::PluginCLI cli("Microchip AT24C02C 2Kbit I2C Serial EEPROM",
                       "Emulates 256-byte I2C serial EEPROM with auto-increment addressing and non-volatile file persistence (--file).");
    auto opt = cli.parse(argc, argv);
    if (opt.show_help) return 0;

    if (opt.socket_path.empty()) {
        std::cerr << "Error: --socket <socket_path> is required.\n\n";
        cli.print_help();
        return 1;
    }

    uint8_t init_val = opt.has_init_val ? static_cast<uint8_t>(opt.init_val) : 0x10;

    // デバイスエミュレータ起動
    I2cEeprom eeprom(0x50, init_val, opt.file_path);
    g_eeprom_instance = &eeprom;

    fbb::PluginCLI::setup_signal_handler(handle_signal);

    std::cout << "[I2C EEPROM] Mock daemon starting on " << opt.socket_path 
              << " (default init-val: 0x" << std::hex << (int)init_val << ")\n";
    std::flush(std::cout);

    // エミュレーション稼働 (接続待ちループ、ブロッキング)
    if (!eeprom.start(opt.socket_path)) {
        std::cerr << "[I2C EEPROM] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[I2C EEPROM] Daemon stopped.\n";
    return 0;
}
