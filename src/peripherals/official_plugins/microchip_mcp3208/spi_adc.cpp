/**
 * @file src/peripherals/official_plugins/microchip_mcp3208/spi_adc.cpp
 * @intent:responsibility
 *   Microchip MCP3208 8 チャンネル 12-bit SPI A/D コンバータのエミュレーションデーモン。
 *   SPI 全二重通信要求（3 バイトシーケンス）を解析し、指定チャンネルのアナログ変換値（0〜4095）を返送する。
 * @intent:rationale
 *   MCP3208 のデータシートプロトコル（Start Bit 0x01, SGL/DIFF, チャンネル番号, Null Bit, 12-bit 出力）を
 *   完全エミュレートし、POSIX 共有メモリ（/spi_adc）を介して Web ダッシュボードのスライダーやテストスクリプトから
 *   非侵襲に任意のアナログ電圧値を動的注入できるようにする。
 */

#include "../../common/spi_slave.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <csignal>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

constexpr size_t ADC_CHANNELS = 8;
constexpr uint16_t ADC_MAX_VAL = 4095; // 12-bit ADC

/**
 * @class SpiAdc
 * @intent:responsibility
 *   MCP3208 プロトコルデコード、8 チャンネルアナログ値の管理、および共有メモリ同期。
 */
class SpiAdc : public SpiSlave {
public:
    /**
     * @brief SPI ADCエミュレータコンストラクタ
     * @param cs チップセレクト番号
     * @param init_val 各チャンネルの初期既定値 (0〜4095)
     * @intent:responsibility 共有メモリ /spi_adc を作成・mmap し、全チャンネルの初期値をセットする。
     */
    SpiAdc(uint8_t cs, uint16_t init_val)
        : SpiSlave(cs), 
          shm_data_(nullptr),
          shm_fd_(-1)
    {
        // 1. ローカルメモリの初期化
        local_channels_.resize(ADC_CHANNELS, init_val);

        // 2. 共有メモリ (/spi_adc) をオープンしてマップ
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

    /**
     * @brief デストラクタ
     * @intent:responsibility 共有メモリの munmap および shm_unlink を行う。
     */
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
     * @param tx_data マスタ送信（Byte 0: Start Bit 0x01, Byte 1: SGL/DIFF + チャンネル番号, Byte 2: ドントケア）
     * @return スレーブ応答（Byte 0: 0x00, Byte 1: 上位 4 ビット, Byte 2: 下位 8 ビット）
     * @intent:responsibility MCP3208 の 3 バイトプロトコルをパースし、指定チャンネルの 12-bit 変換値を全二重返送する。
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
            // ディファレンシャルモードは簡単のため 0
            adc_value = 0;
        }

        // 4. MCP3208 バイト応答構成 (12-bit値を全二重データの3バイトに配置、Null Bit = 0)
        rx_data[0] = 0x00;
        rx_data[1] = static_cast<uint8_t>((adc_value >> 8) & 0x0F); // 上位4ビット (B11-B8)
        rx_data[2] = static_cast<uint8_t>(adc_value & 0xFF);        // 下位8ビット (B7-B0)

        return rx_data;
    }

private:
    /**
     * @brief 指定されたチャンネルのアナログ値を取得する。
     * @intent:responsibility 共有メモリ優先で読み出し、存在しない場合はローカル配列から取得。
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

#include "../../common/cli_helper.hpp"

static SpiAdc* g_adc_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 * @intent:responsibility SIGINT/SIGTERM を受信した際に SPI ADC デーモンを安全に停止・クリーンアップする。
 * @intent:rationale ソケットファイルや共有メモリの孤立・残存プロセス化を防止する。
 * @intent:pre-condition g_adc_instance が初期化されていること。
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_adc_instance) {
        std::cout << "\n[SPI ADC] Stopping daemon safely...\n";
        g_adc_instance->stop();
    }
}

/**
 * @brief MCP3208 8ch SPI ADC エミュレータデーモンエントリポイント
 * @intent:responsibility CLI 引数をパースして SPI ソケット上で ADC エミュレーションデーモンを起動する。
 * @intent:rationale 仮想 SPI バス経由で 12-bit ADC 読み出し要求に応答し、共有メモリからの電圧インジェクションを反映する。
 * @intent:pre-condition --socket オプションで有効なソケットパスが指定されていること。
 */
int main(int argc, char *argv[]) {
    fbb::PluginCLI cli("Microchip MCP3208 8-Ch 12-Bit SPI ADC",
                       "Emulates Microchip MCP3208 12-bit ADC with dynamic channel voltage injection via shared memory (/spi_adc).");
    auto opt = cli.parse(argc, argv);
    if (opt.show_help) return 0;

    if (opt.socket_path.empty()) {
        std::cerr << "Error: --socket <socket_path> is required.\n\n";
        cli.print_help();
        return 1;
    }

    uint16_t init_val = opt.has_init_val ? static_cast<uint16_t>(opt.init_val) : 2048;

    // デバイスエミュレータ起動
    SpiAdc adc(1, init_val);
    g_adc_instance = &adc;

    fbb::PluginCLI::setup_signal_handler(handle_signal);

    std::cout << "[SPI ADC] Mock daemon starting on " << opt.socket_path 
              << " (initial val: " << init_val << ")\n";
    std::flush(std::cout);

    // エミュレーション稼働 (接続待ちループ、ブロッキング)
    if (!adc.start(opt.socket_path)) {
        std::cerr << "[SPI ADC] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[SPI ADC] Daemon stopped.\n";
    return 0;
}
