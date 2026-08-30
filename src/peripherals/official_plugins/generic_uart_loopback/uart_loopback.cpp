/**
 * @file src/peripherals/official_plugins/generic_uart_loopback/uart_loopback.cpp
 * @intent:responsibility
 *   汎用 UART ループバックテスト用エミュレーションデーモン。
 *   PTY スレーブから受信した全バイト列をそのまま transmit() でオウム返し（エコーバック）送信する。
 * @intent:rationale
 *   マルチ UART シナリオ（シナリオ 08 等）において、実機の対向シリアル端末やループバックコネクタを
 *   外部ハードウェア不要で机上再現する。
 */

#include "../../common/uart_device.hpp"
#include <iostream>
#include <cstring>
#include <csignal>

/**
 * @class UartLoopback
 * @intent:responsibility
 *   受信データをそのまま送信バッファへ折り返すエコーバック動作を実装。
 */
class UartLoopback : public UartDevice {
public:
    UartLoopback() : UartDevice() {}

protected:
    /**
     * @brief データ受信時のコールバックハンドラ
     * @param data 受信したデータバイト列
     * @intent:responsibility 受信したバイト列をそのまま PTY スレーブへ即時送出する。
     */
    void onReceive(const std::vector<uint8_t>& data) override {
        if (data.empty()) return;
        
        // 受信したデータをそのまま PTY に送信 (オウム返し)
        transmit(data);
    }
};

#include "../../common/cli_helper.hpp"

// グローバルスコープのシグナル制御用インスタンスポインタ
static UartLoopback* g_uart_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 * @intent:responsibility SIGINT/SIGTERM を受信した際に UART ループバックデーモンを安全に停止する。
 * @intent:rationale PTY デバイスディスクリプタの孤立・残存プロセス化を防止する。
 * @intent:pre-condition g_uart_instance が初期化されていること。
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_uart_instance) {
        std::cout << "\n[UART Loopback] Stopping daemon safely...\n";
        g_uart_instance->stop();
    }
}

/**
 * @brief 汎用 UART ループバックエミュレータデーモンエントリポイント
 * @intent:responsibility CLI 引数をパースして PTY マップファイルを参照し、UART ループバックデーモンを起動する。
 * @intent:rationale 送信されたシリアルバイト列を受信バッファへオウム返し転送する。
 * @intent:pre-condition --pts-file オプションで有効な PTY マップファイルパスが指定されていること。
 */
int main(int argc, char *argv[]) {
    fbb::PluginCLI cli("Generic UART Loopback Device",
                       "Emulates a bi-directional serial terminal loopback connected to master/slave PTY.");
    auto opt = cli.parse(argc, argv);
    if (opt.show_help) return 0;

    std::string pts_file = opt.pts_file.empty() ? opt.file_path : opt.pts_file;

    if (pts_file.empty()) {
        std::cerr << "Error: --pts-file <path> is required.\n\n";
        cli.print_help();
        return 1;
    }

    // デバイスエミュレータ起動
    UartLoopback uart;
    g_uart_instance = &uart;

    fbb::PluginCLI::setup_signal_handler(handle_signal);

    std::cout << "[UART Loopback] Starting loopback daemon using map file: " << pts_file << "\n";
    std::flush(std::cout);

    // エミュレーション稼働 (PTY監視ループ、ブロッキング)
    if (!uart.start(pts_file)) {
        std::cerr << "[UART Loopback] Failed to start loopback daemon.\n";
        return 1;
    }

    std::cout << "[UART Loopback] Daemon stopped.\n";
    return 0;
}
