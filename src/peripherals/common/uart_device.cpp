#include "uart_device.hpp"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <thread>

UartDevice::UartDevice() {}

UartDevice::~UartDevice() {
    stop();
}

bool UartDevice::start(const std::string& pty_map_path) {
    pty_map_path_ = pty_map_path;
    running_ = true;

    std::string pts_name;
    
    std::cout << "[UartDevice] Waiting for PTY name in " << pty_map_path_ << "...\n";
    std::flush(std::cout);

    // 1. PTYスレーブパス名がコントローラによって書き出されるのをポーリング待機 (最大15秒)
    for (int retry = 0; retry < 30; ++retry) {
        if (!running_) return false;

        std::ifstream f(pty_map_path_);
        if (f.is_open()) {
            if (std::getline(f, pts_name)) {
                // 空白文字や改行のトリム
                while (!pts_name.empty() && (pts_name.back() == '\r' || pts_name.back() == '\n' || pts_name.back() == ' ')) {
                    pts_name.pop_back();
                }
                if (!pts_name.empty()) {
                    break;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (pts_name.empty()) {
        std::cerr << "[UartDevice] Error: Failed to read PTY name from " << pty_map_path_ << "\n";
        return false;
    }

    std::cout << "[UartDevice] Opening PTY slave device: " << pts_name << "\n";
    std::flush(std::cout);

    // 2. ブロッキングモードでPTYスレーブをオープン
    pty_fd_ = open(pts_name.c_str(), O_RDWR | O_NOCTTY);
    if (pty_fd_ == -1) {
        std::cerr << "[UartDevice] Error: Failed to open PTY slave " << pts_name << ": " << std::strerror(errno) << "\n";
        return false;
    }

    std::cout << "[UartDevice] Daemon started successfully. Listening for UART data...\n";
    std::flush(std::cout);

    // 3. メインのデータ監視ループを開始
    eventLoop();

    return true;
}

void UartDevice::stop() {
    running_ = false;
    if (pty_fd_ != -1) {
        close(pty_fd_);
        pty_fd_ = -1;
    }
}

void UartDevice::transmit(const std::vector<uint8_t>& data) {
    if (pty_fd_ == -1 || data.empty()) return;
    
    ssize_t written = 0;
    size_t total = data.size();
    
    // 全データバイトが完全に送信されるまでループ書き込み
    while (written < static_cast<ssize_t>(total)) {
        ssize_t r = write(pty_fd_, data.data() + written, total - written);
        if (r <= 0) {
            std::cerr << "[UartDevice] Error writing to PTY: " << std::strerror(errno) << "\n";
            break;
        }
        written += r;
    }
}

void UartDevice::eventLoop() {
    std::vector<uint8_t> buffer(1024);

    while (running_) {
        // PTY からのデータ受信待機 (ブロッキングリード)
        ssize_t bytes_read = read(pty_fd_, buffer.data(), buffer.size());
        if (bytes_read <= 0) {
            // エラーまたはEOF検出時はループ脱出
            if (running_) {
                std::cerr << "[UartDevice] Read error or EOF detected.\n";
            }
            break;
        }

        // 受信したデータを vector にコピーしてイベントハンドラを起動
        std::vector<uint8_t> received_data(buffer.begin(), buffer.begin() + bytes_read);
        onReceive(received_data);
    }
}
