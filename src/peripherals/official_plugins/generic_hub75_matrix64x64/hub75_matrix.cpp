/**
 * @file src/peripherals/official_plugins/generic_hub75_matrix64x64/hub75_matrix.cpp
 * @intent:responsibility
 *   HUB75 規格 64x64 RGB フルカラー LED マトリクスディスプレイの共有メモリフレームバッファ管理デーモン。
 *   幅 x 高さ x 3 (RGB24) バイトの POSIX 共有メモリ（/dev/shm/fbb_hub75_0）を確保・ゼロ初期化し、常駐する。
 * @intent:rationale
 *   Verilog RTL ロジック（DMA/UIO）または C++ エミュレータから直接フレームバッファへ高速書き込みを行い、
 *   Web ダッシュボードの HUB75 Canvas ウィジェットへゼロコピー・60FPS でリアルタイムレンダリングを可能にする。
 */

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <atomic>
#include <chrono>
#include <thread>

#define HUB75_WIDTH 64
#define HUB75_HEIGHT 64
#define HUB75_CHANNELS 3
#define HUB75_FRAME_SIZE (HUB75_WIDTH * HUB75_HEIGHT * HUB75_CHANNELS)
#define HUB75_SHM_PATH "/dev/shm/fbb_hub75_0"

#include "../../common/cli_helper.hpp"

static std::atomic<bool> g_running{true};

/**
 * @brief シグナル受信ハンドラ
 * @intent:responsibility SIGINT/SIGTERM 受信時にデーモンのメインループ停止フラグをセットする。
 * @intent:rationale 共有メモリマッピングの正常解除とファイルディスクリプタのクローズを保証する。
 * @intent:pre-condition なし。
 */
void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

/**
 * @brief HUB75 エミュレータデーモンエントリポイント
 * @param argc 引数の数
 * @param argv 引数配列
 * @intent:responsibility fbb::PluginCLI から解像度と共有メモリパスを取得し、フレームバッファを初期化・待機する。
 * @intent:rationale 共有メモリ経由で高速フレームバッファ描画領域を提供し、Web ダッシュボードへリアルタイム反映する。
 * @intent:pre-condition 有効な共有メモリ名または位置引数が指定されていること。
 */
int main(int argc, char** argv) {
    fbb::PluginCLI cli("Generic HUB75 RGB LED Matrix (64x64 / Daisy Chain)",
                       "Emulates 64x64 (or custom resolution) RGB LED Matrix with direct shared memory frame buffer.");
    auto opt = cli.parse(argc, argv);
    if (opt.show_help) return 0;

    fbb::PluginCLI::setup_signal_handler(signal_handler);

    std::string shm_path = opt.shm_path.empty() ? HUB75_SHM_PATH : opt.shm_path;
    if (shm_path.find("/dev/shm/") != 0 && shm_path.find("/") != 0) {
        shm_path = "/dev/shm/" + shm_path;
    }

    int width = (opt.width > 0) ? opt.width : HUB75_WIDTH;
    int height = (opt.height > 0) ? opt.height : HUB75_HEIGHT;

    size_t frame_size = width * height * HUB75_CHANNELS;

    std::cout << "[HUB75 Matrix] Initializing " << width << "x" << height
              << " RGB LED Matrix Emulator on " << shm_path << "..." << std::endl;

    // Create and initialize shared memory frame buffer
    int shm_fd = open(shm_path.c_str(), O_RDWR | O_CREAT, 0666);
    if (shm_fd < 0) {
        std::cerr << "[HUB75 Matrix ERROR] Failed to open " << shm_path << std::endl;
        return 1;
    }

    if (ftruncate(shm_fd, frame_size) != 0) {
        std::cerr << "[HUB75 Matrix ERROR] Failed to ftruncate " << shm_path << std::endl;
        close(shm_fd);
        return 1;
    }

    uint8_t* frame_ptr = (uint8_t*)mmap(NULL, frame_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (frame_ptr == MAP_FAILED) {
        std::cerr << "[HUB75 Matrix ERROR] Failed to mmap " << shm_path << std::endl;
        close(shm_fd);
        return 1;
    }

    // Fill initial frame buffer with 0 (all LEDs off)
    std::memset(frame_ptr, 0, frame_size);

    std::cout << "[HUB75 Matrix] Shared memory " << shm_path << " ready ("
              << frame_size << " bytes mapped)." << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[HUB75 Matrix] Shutting down HUB75 emulator daemon..." << std::endl;
    munmap(frame_ptr, HUB75_FRAME_SIZE);
    close(shm_fd);
    return 0;
}
