#include "../common/uart_device.hpp"
#include <iostream>
#include <cstring>
#include <csignal>

class UartLoopback : public UartDevice {
public:
    UartLoopback() : UartDevice() {}

protected:
    /**
     * @brief データ受信時のコールバックハンドラ
     * 受信したデータをそのままオウム返し (ループバック) して送信します。
     * @param data 受信したデータバイト列
     */
    void onReceive(const std::vector<uint8_t>& data) override {
        if (data.empty()) return;
        
        // 受信したデータをそのまま PTY に送信 (オウム返し)
        transmit(data);
    }
};

// グローバルスコープのシグナル制御用インスタンスポインタ
static UartLoopback* g_uart_instance = nullptr;

/**
 * @brief シグナル受信時の安全なクリーンアップハンドラ
 */
void handle_signal(int sig) {
    (void)sig;
    if (g_uart_instance) {
        std::cout << "\n[UART Loopback] Stopping daemon safely...\n";
        g_uart_instance->stop();
    }
}

int main(int argc, char *argv[]) {
    std::string pts_file;

    // コマンドライン引数解析
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--pts-file") == 0 && i + 1 < argc) {
            pts_file = argv[++i];
        }
    }

    if (pts_file.empty()) {
        std::cerr << "Usage: " << argv[0] << " --pts-file <path_to_vfpga_uart_X_file>\n"
                  << "Options:\n"
                  << "  --pts-file    Path to the temporary file containing the allocated PTY slave path (Required)\n";
        return 1;
    }

    // デバイスエミュレータ起動
    UartLoopback uart;
    g_uart_instance = &uart;

    // クリーンアップ用のシグナルハンドリング
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

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
