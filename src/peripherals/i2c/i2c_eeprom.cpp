#include "../common/i2c_slave.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <csignal>

constexpr size_t EEPROM_SIZE = 256;

class I2cEeprom : public I2cSlave {
public:
    /**
     * @brief EEPROMエミュレータコンストラクタ
     * @param dev_addr I2Cデバイスアドレス
     * @param init_val メモリセルのデフォルト初期値
     * @param mock_file 不揮発状態を保存するファイルパス (空の場合は不揮発保存を行わない)
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
     * 1バイト目: メモリアドレスポインタのセット
     * 2バイト目以降: アドレスポインタからの連続データ書き込み
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
     * 現在のアドレスポインタからデータを読み出し、読み出し毎にポインタをオートインクリメントします。
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
    std::vector<uint8_t> memory_;      ///< 256バイトのEEPROMメモリセル
    size_t current_addr_{0};           ///< 現在の読み書きアドレスポインタ
};

// グローバルスコープのシグナル制御用インスタンスポインタ
static I2cEeprom* g_eeprom_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_eeprom_instance) {
        std::cout << "\n[I2C EEPROM] Stopping daemon safely...\n";
        g_eeprom_instance->stop();
    }
}

int main(int argc, char *argv[]) {
    std::string sock_file;
    std::string mock_file;
    uint8_t init_val = 0x10; // デフォルト初期値 (10進数: 16)

    // コマンドライン引数解析
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            sock_file = argv[++i];
        } else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            mock_file = argv[++i];
        } else if (std::strcmp(argv[i], "--init-val") == 0 && i + 1 < argc) {
            init_val = static_cast<uint8_t>(std::strtol(argv[++i], nullptr, 0));
        }
    }

    if (sock_file.empty()) {
        std::cerr << "Usage: " << argv[0] << " --socket <socket_path> [--file <mock_file>] [--init-val <val>]\n"
                  << "Options:\n"
                  << "  --socket    UNIX domain socket path (Required)\n"
                  << "  --file      Persistence backing file path for the memory cells (Optional)\n"
                  << "  --init-val  Initial value for memory cell 0 (Default: 0x10) (Optional)\n";
        return 1;
    }

    // デバイスエミュレータ起動
    I2cEeprom eeprom(0x50, init_val, mock_file);
    g_eeprom_instance = &eeprom;

    // クリーンアップ用のシグナルハンドリング
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "[I2C EEPROM] Mock daemon starting on " << sock_file 
              << " (default init-val: 0x" << std::hex << (int)init_val << ")\n";
    std::flush(std::cout);

    // エミュレーション稼働 (接続待ちループ、ブロッキング)
    if (!eeprom.start(sock_file)) {
        std::cerr << "[I2C EEPROM] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[I2C EEPROM] Daemon stopped.\n";
    return 0;
}
