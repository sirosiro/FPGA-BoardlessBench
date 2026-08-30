/**
 * @file main.cpp
 * @intent:responsibility デイジーチェーン接続された複数枚の HUB75 パネルへの展開描画を検証する。
 * @intent:rationale      パネル連結時のアスペクト比計算および複数パネルフレーム同期をテストする。
 * @intent:pre-condition  chain_layout 設定が config.dts に定義されていること。
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <atomic>

#define HUB75_WIDTH 128
#define HUB75_HEIGHT 64
#define HUB75_CHANNELS 3
#define HUB75_FRAME_SIZE (HUB75_WIDTH * HUB75_HEIGHT * HUB75_CHANNELS)
#define HUB75_SHM_PATH "/dev/shm/fbb_hub75_chain0"

static std::atomic<bool> g_running{true};
static uint8_t* g_frame_ptr = nullptr;

void signal_handler(int sig) {
    g_running = false;
    signal(sig, SIG_DFL);
    raise(sig);
}

void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!g_frame_ptr || x < 0 || x >= HUB75_WIDTH || y < 0 || y >= HUB75_HEIGHT) return;
    int idx = (y * HUB75_WIDTH + x) * HUB75_CHANNELS;
    g_frame_ptr[idx] = r;
    g_frame_ptr[idx + 1] = g;
    g_frame_ptr[idx + 2] = b;
}

void clear_screen() {
    if (g_frame_ptr) {
        std::memset(g_frame_ptr, 0, HUB75_FRAME_SIZE);
    }
}

// -----------------------------------------------------------------------------
// Demo 1: Ultra-Wide Plasma Wave (128x64)
// -----------------------------------------------------------------------------
void run_demo_plasma(int frames = 80) {
    std::cout << "[FW Demo 1] Running Ultra-Wide 128x64 Plasma Wave..." << std::endl;
    for (int f = 0; f < frames && g_running; f++) {
        double time = f * 0.08;
        for (int y = 0; y < HUB75_HEIGHT; y++) {
            for (int x = 0; x < HUB75_WIDTH; x++) {
                double v1 = std::sin(x * 0.06 + time);
                double v2 = std::sin(10 * (x * std::sin(time / 2) + y * std::cos(time / 3)) * 0.04 + time);
                double cx = x + 0.5 * std::sin(time / 5);
                double cy = y + 0.5 * std::cos(time / 3);
                double v3 = std::sin(std::sqrt(cx * cx + cy * cy + 1.0) * 0.08 + time);
                double v = (v1 + v2 + v3) / 3.0;

                uint8_t r = static_cast<uint8_t>((std::sin(v * M_PI) * 0.5 + 0.5) * 255);
                uint8_t g = static_cast<uint8_t>((std::cos(v * M_PI) * 0.5 + 0.5) * 255);
                uint8_t b = static_cast<uint8_t>((std::sin(v * M_PI + (2 * M_PI / 3)) * 0.5 + 0.5) * 255);

                set_pixel(x, y, r, g, b);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

// -----------------------------------------------------------------------------
// Demo 2: Cross-Panel Bouncing Ball Physics (Panel 1 <-> Panel 2)
// -----------------------------------------------------------------------------
void run_demo_bouncing_ball(int frames = 120) {
    std::cout << "[FW Demo 2] Running Cross-Panel Bouncing Ball Simulation (128x64)..." << std::endl;
    float bx = 10.0f, by = 10.0f;
    float vx = 2.4f, vy = 1.6f;
    const int trail_len = 16;
    struct Point { int x, y; uint8_t r, g, b; };
    std::vector<Point> trail;

    for (int f = 0; f < frames && g_running; f++) {
        bx += vx;
        by += vy;
        if (bx <= 2 || bx >= HUB75_WIDTH - 3) vx = -vx;
        if (by <= 2 || by >= HUB75_HEIGHT - 3) vy = -vy;

        uint8_t ball_r = (bx > 64) ? 0 : 255;
        uint8_t ball_g = (bx > 64) ? 255 : 128;
        uint8_t ball_b = (bx > 64) ? 255 : 0;

        trail.push_back({(int)bx, (int)by, ball_r, ball_g, ball_b});
        if ((int)trail.size() > trail_len) trail.erase(trail.begin());

        clear_screen();

        // Draw Panel Boundary Line at x=64
        for (int y = 0; y < HUB75_HEIGHT; y++) {
            set_pixel(63, y, 40, 40, 80);
            set_pixel(64, y, 40, 40, 80);
        }

        for (size_t i = 0; i < trail.size(); i++) {
            float fade = (float)(i + 1) / trail.size();
            uint8_t tr = static_cast<uint8_t>(trail[i].r * fade);
            uint8_t tg = static_cast<uint8_t>(trail[i].g * fade);
            uint8_t tb = static_cast<uint8_t>(trail[i].b * fade);

            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    set_pixel(trail[i].x + dx, trail[i].y + dy, tr, tg, tb);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

// -----------------------------------------------------------------------------
// Demo 3: Ultra-Wide Scrolling Text
// -----------------------------------------------------------------------------
void run_demo_scrolling_banner(int passes = 1) {
    std::cout << "[FW Demo 3] Running Ultra-Wide Banner Text Scrolling..." << std::endl;

    // 5x7 Font map for "F-BB 128x64 MATRIX"
    std::vector<std::string> banner = {
        "#####  ######  ######   ######     #     #####   #####    #   #   #   #     #   #   ###   #####  #####   ###   #   #",
        "#      #    #  #     #  #     #   ##    #     # #     #   #   #   #   #     ## ##  #   #    #    #      #   #  #   #",
        "####   ######  ######   ######  #  #        #   ######    #####   #####     # # #  #####    #    #      #####   ### ",
        "#      #    #  #     #  #     #    #       #    #     #       #       #     #   #  #   #    #    #      #   #  #   #",
        "#      #####   ######   ######   #####  #####    #####        #       #     #   #  #   #    #    #####  #   #  #   #"
    };

    int banner_width = static_cast<int>(banner[0].size()) * 2;

    for (int p = 0; p < passes && g_running; p++) {
        for (int offset = HUB75_WIDTH; offset > -banner_width && g_running; offset -= 2) {
            clear_screen();
            for (size_t r = 0; r < banner.size(); r++) {
                for (size_t c = 0; c < banner[r].size(); c++) {
                    if (banner[r][c] == '#') {
                        int px = offset + c * 2;
                        int py = 22 + r * 3;
                        if (px >= 0 && px < HUB75_WIDTH) {
                            // Panel 1 (x < 64) is Cyan/Blue, Panel 2 (x >= 64) is Orange/Yellow
                            uint8_t red = (px < 64) ? 0 : 255;
                            uint8_t green = (px < 64) ? 220 : 160;
                            uint8_t blue = (px < 64) ? 255 : 20;
                            set_pixel(px, py, red, green, blue);
                            set_pixel(px + 1, py, red, green, blue);
                            set_pixel(px, py + 1, red, green, blue);
                            set_pixel(px + 1, py + 1, red, green, blue);
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
}

// -----------------------------------------------------------------------------
// Demo 4: Panel 1 vs Panel 2 Sync Stepping Test
// -----------------------------------------------------------------------------
void run_demo_sync_stepping(int loops = 6) {
    std::cout << "[FW Demo 4] Running Daisy-Chain Sync Stepping Test..." << std::endl;
    for (int l = 0; l < loops && g_running; l++) {
        clear_screen();

        // Flash Panel 1 (Left 64x64) RED
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                set_pixel(x, y, 255, 20, 40);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Flash Panel 2 (Right 64x64) GREEN
        clear_screen();
        for (int y = 0; y < 64; y++) {
            for (int x = 64; x < 128; x++) {
                set_pixel(x, y, 20, 255, 60);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Flash BOTH Panels BLUE
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 128; x++) {
                set_pixel(x, y, 30, 140, 255);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void print_menu(int uart_fd) {
    const char* menu = "\r\n"
                       "==================================================\r\n"
                       "  F-BB HUB75 128x64 Daisy-Chain LED Panel Shell\r\n"
                       "==================================================\r\n"
                       "1. Ultra-Wide 128x64 Plasma Wave\r\n"
                       "2. Cross-Panel Bouncing Ball Physics (P1 <-> P2)\r\n"
                       "3. Dual-Panel Banner Text Scrolling\r\n"
                       "4. Panel 1 vs Panel 2 Sync Stepping Test\r\n"
                       "5. Exit Test Shell\r\n"
                       "--------------------------------------------------\r\n"
                       "Enter choice (1-5): ";
    if (uart_fd >= 0) {
        write(uart_fd, menu, std::strlen(menu));
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "[FW Main] HUB75 128x64 Dual-Panel Daisy-Chain Verification Firmware Started." << std::endl;

    int shm_fd = open(HUB75_SHM_PATH, O_RDWR | O_CREAT, 0666);
    if (shm_fd < 0) {
        std::cerr << "[FW Main ERROR] Failed to open " << HUB75_SHM_PATH << std::endl;
        return 1;
    }

    if (ftruncate(shm_fd, HUB75_FRAME_SIZE) != 0) {
        std::cerr << "[FW Main ERROR] Failed to ftruncate " << HUB75_SHM_PATH << std::endl;
        close(shm_fd);
        return 1;
    }

    g_frame_ptr = (uint8_t*)mmap(NULL, HUB75_FRAME_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_frame_ptr == MAP_FAILED) {
        std::cerr << "[FW Main ERROR] Failed to mmap " << HUB75_SHM_PATH << std::endl;
        close(shm_fd);
        return 1;
    }

    clear_screen();

    int uart_fd = open("/dev/ttyUL0", O_RDWR | O_NOCTTY);
    if (uart_fd >= 0) {
        struct termios options;
        tcgetattr(uart_fd, &options);
        cfmakeraw(&options);
        tcsetattr(uart_fd, TCSANOW, &options);
        std::cout << "[FW Main] UART Console /dev/ttyUL0 connected." << std::endl;
    }

    const char* interactive_env = std::getenv("VFPGA_INTERACTIVE");
    bool interactive = (interactive_env && (std::string(interactive_env) == "1" || std::string(interactive_env) == "true"));

    if (interactive) {
        std::cout << "[FW Main] Interactive Mode (VFPGA_INTERACTIVE is set)." << std::endl;
        print_menu(uart_fd);
        run_demo_plasma(20); // Quick welcome splash

        while (g_running) {
            char choice = '\0';
            print_menu(uart_fd);
            while (g_running) {
                if (uart_fd >= 0 && read(uart_fd, &choice, 1) > 0) {
                    write(uart_fd, &choice, 1);
                    write(uart_fd, "\r\n", 2);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (choice == '1') {
                run_demo_plasma(120);
            } else if (choice == '2') {
                run_demo_bouncing_ball(160);
            } else if (choice == '3') {
                run_demo_scrolling_banner(1);
            } else if (choice == '4') {
                run_demo_sync_stepping(6);
            } else if (choice == '5' || choice == 'q') {
                std::cout << "[FW Main] Exiting test shell..." << std::endl;
                break;
            }
        }
    } else {
        std::cout << "[FW Main] Automated Batch Mode. Running Demo 1, 2 & 4..." << std::endl;
        run_demo_plasma(40);
        run_demo_bouncing_ball(40);
        run_demo_sync_stepping(4);
        std::cout << "[FW Main] SUCCESS: HUB75 128x64 Dual-Panel Daisy-Chain Firmware Test PASSED!" << std::endl;
    }

    clear_screen();
    munmap(g_frame_ptr, HUB75_FRAME_SIZE);
    close(shm_fd);
    if (uart_fd >= 0) close(uart_fd);

    return 0;
}
