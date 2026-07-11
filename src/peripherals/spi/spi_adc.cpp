#include "../common/spi_slave.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <csignal>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

constexpr size_t ADC_CHANNELS = 8;
constexpr uint16_t ADC_MAX_VAL = 4095; // 12-bit ADC

class SpiAdc : public SpiSlave {
public:
    /**
     * @brief SPI ADCエミュレータコンストラクタ
     * @param cs チップセレクト番号
     * @param init_val 各チャンネルの初期既定値
     */
    SpiAdc(uint8_t cs, uint16_t init_val)
        : SpiSlave(cs), 
          shm_data_(nullptr),
          shm_fd_(-1)
    {
        // 1. ローカルメモリの初期化
        local_channels_.resize(ADC_CHANNELS, init_val);

        // 2. 共有メモリ (/tmp/spi_adc を指す共有メモリファイル) をオープンしてマップ
        // ダッシュボードUIや自動テストインジェクタから動的に電圧値を書き込むための領域
        shm_fd_ = shm_open("/spi_adc", O_RDWR | O_CREAT, 0666);
        if (shm_fd_ != -1) {
            if (ftruncate(shm_fd_, ADC_CHANNELS * sizeof(uint16_t)) != -1) {
                void* ptr = mmap(nullptr, ADC_CHANNELS * sizeof(uint16_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
                if (ptr != MAP_FAILED) {
                    shm_data_ = static_cast<uint16_t*>(ptr);
                    // 初期値の同期
                    for (size_t i = 0; i < ADC_CHANNELS; ++i) {
                        shm_data_[i] = init_val;
                    }
                    std::cout << "[SPI ADC] Shared memory mapped successfully at /dev/shm/spi_adc\n";
                }
            }
        }
        if (!shm_data_) {
            std::cerr << "[SPI ADC] Warning: Failed to map shared memory. Falling back to local emulation.\n";
        }
    }

    ~SpiAdc() override {
        if (shm_data_) {
            munmap(shm_data_, ADC_CHANNELS * sizeof(uint16_t));
        }
        if (shm_fd_ != -1) {
            close(shm_fd_);
            shm_unlink("/spi_adc");
        }
    }

protected:
    /**
     * @brief SPI全二重転送のシミュレーション
     * MCP3208 のデータ転送フォーマットを再現します。
     * マスタ送信: 
     *   Byte 0: Start Bit (通常 0x01)
     *   Byte 1: [SGL/DIFF, D2, D1, D0, X, X, X, X] (D2-D0 がチャンネル指定)
     *   Byte 2: ドントケア
     * スレーブ応答:
     *   Byte 0: 0x00
     *   Byte 1: [0, 0, 0, 0, NullBit(0), B11, B10, B9]
     *   Byte 2: [B8, B7, B6, B5, B4, B3, B2, B1, B0]
     */
    std::vector<uint8_t> onTransfer(const std::vector<uint8_t>& tx_data) override {
        std::vector<uint8_t> rx_data(tx_data.size(), 0);
        if (tx_data.size() < 3) return rx_data; // MCP3208は最低3バイト必要

        // 1. スタートビットの確認
        uint8_t start_bit = tx_data[0];
        if (start_bit != 0x01) {
            return rx_data; // スタートビットがなければ0を応答
        }

        // 2. チャンネル番号と設定の抽出
        uint8_t config = tx_data[1];
        bool single_ended = (config & 0x80) != 0;
        uint8_t channel = (config >> 4) & 0x07;

        // 3. 変換値の取得
        uint16_t adc_value = 0;
        if (single_ended && channel < ADC_CHANNELS) {
            adc_value = getChannelValue(channel);
        } else {
            // ディファレンシャルモードは簡単のため、CH0-CH1差分等を適宜返す (ここでは簡易的に0)
            adc_value = 0;
        }

        // 4. MCP3208 バイト応答構成
        // 12-bit値を全二重データの3バイトに配置 (Null Bit = 0)
        rx_data[0] = 0x00;
        rx_data[1] = static_cast<uint8_t>((adc_value >> 8) & 0x0F); // 上位4ビット (B11-B8)
        rx_data[2] = static_cast<uint8_t>(adc_value & 0xFF);        // 下位8ビット (B7-B0)

        return rx_data;
    }

private:
    /**
     * @brief 指定されたチャンネルのアナログ値を取得
     */
    uint16_t getChannelValue(uint8_t channel) {
        uint16_t val = 0;
        if (shm_data_) {
            val = shm_data_[channel];
        } else {
            val = local_channels_[channel];
        }
        return (val > ADC_MAX_VAL) ? ADC_MAX_VAL : val;
    }

    uint16_t* shm_data_;               ///< 共有メモリポインタ
    int shm_fd_;                       ///< 共有メモリファイルディスクリプタ
    std::vector<uint16_t> local_channels_; ///< ローカルフォールバック用チャンネルデータ
};

// グローバルスコープのシグナル制御用インスタンスポインタ
static SpiAdc* g_adc_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_adc_instance) {
        std::cout << "\n[SPI ADC] Stopping daemon safely...\n";
        g_adc_instance->stop();
    }
}

int main(int argc, char *argv[]) {
    std::string sock_file;
    uint16_t init_val = 2048; // デフォルト初期値 (12-bit の中央値 2048 = 約 1.65V)

    // コマンドライン引数解析
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            sock_file = argv[++i];
        } else if (std::strcmp(argv[i], "--init-val") == 0 && i + 1 < argc) {
            init_val = static_cast<uint16_t>(std::strtol(argv[++i], nullptr, 0));
        }
    }

    if (sock_file.empty()) {
        std::cerr << "Usage: " << argv[0] << " --socket <socket_path> [--init-val <val>]\n"
                  << "Options:\n"
                  << "  --socket    UNIX domain socket path (Required)\n"
                  << "  --init-val  Initial ADC digital value for all channels (Default: 2048) (Optional)\n";
        return 1;
    }

    // デバイスエミュレータ起動
    SpiAdc adc(1, init_val);
    g_adc_instance = &adc;

    // クリーンアップ用のシグナルハンドリング
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "[SPI ADC] Mock daemon starting on " << sock_file 
              << " (initial val: " << init_val << ")\n";
    std::flush(std::cout);

    // エミュレーション稼働 (接続待ちループ、ブロッキング)
    if (!adc.start(sock_file)) {
        std::cerr << "[SPI ADC] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[SPI ADC] Daemon stopped.\n";
    return 0;
}
