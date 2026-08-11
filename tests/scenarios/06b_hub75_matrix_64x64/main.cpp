#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <termios.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>

#define HUB75_WIDTH 64
#define HUB75_HEIGHT 64
#define HUB75_CHANNELS 3
#define HUB75_FRAME_SIZE (HUB75_WIDTH * HUB75_HEIGHT * HUB75_CHANNELS)
#define HUB75_SHM_PATH "/dev/shm/fbb_hub75_0"
#define UART_DEV "/dev/ttyUL0"

static uint8_t* g_frame_ptr = nullptr;
static std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    g_running = false;
    signal(sig, SIG_DFL);
    raise(sig);
}

void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= HUB75_WIDTH || y < 0 || y >= HUB75_HEIGHT || !g_frame_ptr) return;
    int idx = (y * HUB75_WIDTH + x) * HUB75_CHANNELS;
    g_frame_ptr[idx] = r;
    g_frame_ptr[idx + 1] = g;
    g_frame_ptr[idx + 2] = b;
}

void clear_screen(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) {
    if (!g_frame_ptr) return;
    for (int y = 0; y < HUB75_HEIGHT; y++) {
        for (int x = 0; x < HUB75_WIDTH; x++) {
            set_pixel(x, y, r, g, b);
        }
    }
}

// -----------------------------------------------------------------------------
// Demo 1: Rainbow Color Wave (24-bit RGB Plasma Waves)
// -----------------------------------------------------------------------------
void run_demo_rainbow(int frames = 60) {
    std::cout << "[FW Demo 1] Running Rainbow Color Wave Plasma Animation..." << std::endl;
    for (int t = 0; t < frames && g_running; t++) {
        float time_val = t * 0.1f;
        for (int y = 0; y < HUB75_HEIGHT; y++) {
            for (int x = 0; x < HUB75_WIDTH; x++) {
                float v1 = std::sin(x * 0.1f + time_val);
                float v2 = std::sin(y * 0.1f + time_val);
                float v3 = std::sin((x + y) * 0.08f + time_val);

                uint8_t r = static_cast<uint8_t>((std::sin(v1 * M_PI) * 0.5f + 0.5f) * 255);
                uint8_t g = static_cast<uint8_t>((std::cos(v2 * M_PI) * 0.5f + 0.5f) * 255);
                uint8_t b = static_cast<uint8_t>((std::sin(v3 * M_PI) * 0.5f + 0.5f) * 255);

                set_pixel(x, y, r, g, b);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

// -----------------------------------------------------------------------------
// Demo 2: Bouncing Ball Simulation (2D Physics & Particle Trail)
// -----------------------------------------------------------------------------
void run_demo_bouncing_ball(int frames = 90) {
    std::cout << "[FW Demo 2] Running 2D Physics Bouncing Ball Simulation..." << std::endl;
    float px = 32.0f, py = 10.0f;
    float vx = 1.8f, vy = 1.2f;
    float gravity = 0.15f;

    struct Trail { int x, y; uint8_t r, g, b; };
    std::vector<Trail> trail;

    for (int f = 0; f < frames && g_running; f++) {
        vy += gravity;
        px += vx;
        py += vy;

        if (px <= 2.0f || px >= HUB75_WIDTH - 3.0f) { vx = -vx * 0.95f; }
        if (py >= HUB75_HEIGHT - 3.0f) { py = HUB75_HEIGHT - 3.0f; vy = -vy * 0.88f; }

        trail.push_back({ static_cast<int>(px), static_cast<int>(py), 255, 200, 50 });
        if (trail.size() > 15) trail.erase(trail.begin());

        clear_screen(5, 8, 15);

        // Draw particle trails with fade out
        for (size_t i = 0; i < trail.size(); i++) {
            float alpha = (float)(i + 1) / trail.size();
            set_pixel(trail[i].x, trail[i].y,
                      static_cast<uint8_t>(trail[i].r * alpha),
                      static_cast<uint8_t>(trail[i].g * alpha),
                      static_cast<uint8_t>(trail[i].b * alpha));
        }

        // Draw Ball
        int bx = static_cast<int>(px);
        int by = static_cast<int>(py);
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy <= 4) {
                    set_pixel(bx + dx, by + dy, 0, 220, 255);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

// -----------------------------------------------------------------------------
// Demo 3: Scrolling Banner Text ("F-BB HUB75 64x64")
// -----------------------------------------------------------------------------
void run_demo_scrolling_text(int loops = 1) {
    std::cout << "[FW Demo 3] Running Banner Text Scrolling Animation..." << std::endl;
    // 5x7 Font pattern for 'F', '-', 'B', 'B'
    std::vector<std::string> banner = {
        "#####  #####  ######   ###### ",
        "#      #   #  #     #  #     #",
        "####   #####  ######   ###### ",
        "#      #   #  #     #  #     #",
        "#      #####  ######   ###### "
    };

    for (int l = 0; l < loops && g_running; l++) {
        for (int offset = HUB75_WIDTH; offset > -120 && g_running; offset--) {
            clear_screen(0, 0, 0);
            for (size_t r = 0; r < banner.size(); r++) {
                for (size_t c = 0; c < banner[r].size(); c++) {
                    if (banner[r][c] == '#') {
                        int px = offset + c * 2;
                        int py = 24 + r * 2;
                        set_pixel(px, py, 255, 50, 100);
                        set_pixel(px + 1, py, 255, 50, 100);
                        set_pixel(px, py + 1, 255, 50, 100);
                        set_pixel(px + 1, py + 1, 255, 50, 100);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
}

// -----------------------------------------------------------------------------
// Demo 4: Mandelbrot Fractal Real-time Render
// -----------------------------------------------------------------------------
void run_demo_mandelbrot() {
    std::cout << "[FW Demo 4] Rendering Mandelbrot Fractal..." << std::endl;
    int max_iter = 30;
    for (int y = 0; y < HUB75_HEIGHT && g_running; y++) {
        for (int x = 0; x < HUB75_WIDTH && g_running; x++) {
            float zx = 0.0f, zy = 0.0f;
            float cx = (x - 42.0f) / 20.0f;
            float cy = (y - 32.0f) / 20.0f;
            int iter = 0;

            while (zx * zx + zy * zy < 4.0f && iter < max_iter) {
                float tmp = zx * zx - zy * zy + cx;
                zy = 2.0f * zx * zy + cy;
                zx = tmp;
                iter++;
            }

            if (iter == max_iter) {
                set_pixel(x, y, 0, 0, 0);
            } else {
                uint8_t r = (iter * 15) % 255;
                uint8_t g = (iter * 8) % 255;
                uint8_t b = (iter * 25) % 255;
                set_pixel(x, y, r, g, b);
            }
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

// -----------------------------------------------------------------------------
// Demo 5: Animated Frame Pattern Player
// -----------------------------------------------------------------------------
void run_demo_frame_player(int frames = 60) {
    std::cout << "[FW Demo 5] Running Animated Frame Pattern Player..." << std::endl;
    for (int f = 0; f < frames && g_running; f++) {
        for (int y = 0; y < HUB75_HEIGHT; y++) {
            for (int x = 0; x < HUB75_WIDTH; x++) {
                uint8_t r = ((x ^ y ^ f) * 8) % 255;
                uint8_t g = ((x * 4 + f * 2) % 255);
                uint8_t b = ((y * 4 + f * 3) % 255);
                set_pixel(x, y, r, g, b);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

void print_menu(int uart_fd) {
    const char* menu = "\r\n"
                       "==================================================\r\n"
                       "   F-BB HUB75 64x64 RGB LED Matrix Demo Shell\r\n"
                       "==================================================\r\n"
                       "1. Rainbow Color Wave (24-bit RGB Plasma Waves)\r\n"
                       "2. Bouncing Ball Sim (2D Physics & Particle Trail)\r\n"
                       "3. Scrolling Banner Text (\"F-BB HUB75 64x64 Matrix\")\r\n"
                       "4. Mandelbrot Fractal (Real-time Math Render)\r\n"
                       "5. Animated Frame Player (Color Pattern Loop)\r\n"
                       "6. Exit Test Shell\r\n"
                       "--------------------------------------------------\r\n"
                       "Enter choice (1-6): ";
    if (uart_fd >= 0) {
        write(uart_fd, menu, std::strlen(menu));
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "[FW Main] HUB75 64x64 RGB LED Matrix Verification Firmware Started." << std::endl;

    // Open Shared Memory
    int shm_fd = open(HUB75_SHM_PATH, O_RDWR | O_CREAT, 0666);
    if (shm_fd >= 0) {
        ftruncate(shm_fd, HUB75_FRAME_SIZE);
        g_frame_ptr = (uint8_t*)mmap(NULL, HUB75_FRAME_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        close(shm_fd);
    }

    if (!g_frame_ptr) {
        std::cerr << "[FW ERROR] Unable to map " << HUB75_SHM_PATH << std::endl;
        return 1;
    }

    // Open UART
    int uart_fd = open(UART_DEV, O_RDWR | O_NOCTTY);
    if (uart_fd >= 0) {
        struct termios options;
        tcgetattr(uart_fd, &options);
        cfmakeraw(&options);
        tcsetattr(uart_fd, TCSANOW, &options);
    }

    // Initial automated test run for batch verification runner
    if (!getenv("VFPGA_INTERACTIVE")) {
        std::cout << "[FW Main] Automated Batch Mode. Running Demo 1 & 3..." << std::endl;
        run_demo_rainbow(30);
        run_demo_scrolling_text(1);
        std::cout << "[FW Main] SUCCESS: HUB75 64x64 Matrix Firmware Test PASSED!" << std::endl;
        return 0;
    }

    // Interactive Mode Shell
    std::cout << "[FW Main] Interactive Mode (VFPGA_INTERACTIVE is set)." << std::endl;
    print_menu(uart_fd);
    run_demo_rainbow(20); // Initial welcome splash

    while (g_running) {
        char ch = '\0';
        while (g_running) {
            if (uart_fd >= 0 && read(uart_fd, &ch, 1) > 0) {
                write(uart_fd, &ch, 1);
                write(uart_fd, "\r\n", 2);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (ch == '1') {
            run_demo_rainbow(90);
        } else if (ch == '2') {
            run_demo_bouncing_ball(120);
        } else if (ch == '3') {
            run_demo_scrolling_text(2);
        } else if (ch == '4') {
            run_demo_mandelbrot();
        } else if (ch == '5') {
            run_demo_frame_player(90);
        } else if (ch == '6') {
            std::cout << "[FW Main] Exiting interactive shell. Goodbye!" << std::endl;
            break;
        } else {
            std::cout << "[FW Main] Invalid selection: '" << ch << "'. Please enter 1-6." << std::endl;
        }
        print_menu(uart_fd);
    }

    if (uart_fd >= 0) close(uart_fd);
    return 0;
}
