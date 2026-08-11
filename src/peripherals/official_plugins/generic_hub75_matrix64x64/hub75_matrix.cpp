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

static std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string shm_path = HUB75_SHM_PATH;
    int width = HUB75_WIDTH;
    int height = HUB75_HEIGHT;

    if (argc >= 2) {
        shm_path = argv[1];
        if (shm_path.find("/dev/shm/") != 0) {
            shm_path = "/dev/shm/" + shm_path;
        }
    }
    if (argc >= 3) {
        width = std::stoi(argv[2]);
    }
    if (argc >= 4) {
        height = std::stoi(argv[3]);
    }

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
