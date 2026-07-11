#include "i2c_slave.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>

I2cSlave::I2cSlave(uint8_t dev_addr) : dev_addr_(dev_addr) {}

I2cSlave::~I2cSlave() {
    stop();
}

bool I2cSlave::start(const std::string& socket_path) {
    socket_path_ = socket_path;
    running_ = true;

    // 1. UNIXドメインソケットの作成
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        std::cerr << "[I2cSlave] Error: Failed to create socket\n";
        return false;
    }

    // 以前のソケットファイルの残骸をクリーンアップ
    unlink(socket_path_.c_str());

    // 2. アドレス構造体の設定とbind
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "[I2cSlave] Error: Failed to bind socket to " << socket_path_ << "\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // 3. 待ち受け開始
    if (listen(server_fd_, 5) == -1) {
        std::cerr << "[I2cSlave] Error: Failed to listen on socket\n";
        stop();
        return false;
    }

    std::cout << "[I2cSlave] Daemon listening on " << socket_path_ << "\n";
    std::flush(std::cout);

    // 4. クライアント接続待ちループ
    while (running_) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd == -1) {
            if (!running_) break;
            continue;
        }
        // 同期接続処理
        handleClient(client_fd);
    }

    return true;
}

void I2cSlave::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

void I2cSlave::handleClient(int client_fd) {
    while (running_) {
        uint16_t i2c_addr = 0;
        uint16_t flags = 0;
        uint16_t len = 0;

        // 1. I2Cメッセージヘッダー (アドレス、フラグ、長さ) の受信
        // ソケットクローズ時は 0 が返るため早期リターン
        if (recv(client_fd, &i2c_addr, sizeof(i2c_addr), 0) <= 0) break;
        if (recv(client_fd, &flags, sizeof(flags), 0) <= 0) break;
        if (recv(client_fd, &len, sizeof(len), 0) <= 0) break;

        // 2. 読み書き判定
        if (flags & 0x0001) { // I2C_M_RD (読み出し処理)
            // 仮想ペリフェラルから読み出しデータを取得
            std::vector<uint8_t> read_data = onRead(len);
            
            // 安全対策：要求された長さと異なる場合はサイズを補正して送信
            if (read_data.size() < len) {
                read_data.resize(len, 0);
            }
            send(client_fd, read_data.data(), len, 0);
        } else { // 書き込み処理
            std::vector<uint8_t> write_data(len);
            if (len > 0) {
                ssize_t received = 0;
                // 要求されたバイト数を完全に受信するまでループ
                while (received < len) {
                    ssize_t r = recv(client_fd, write_data.data() + received, len - received, 0);
                    if (r <= 0) break;
                    received += r;
                }
                if (received < len) break; // 通信切断
            }
            // 仮想ペリフェラルの書き込み処理を実行
            onWrite(write_data);
        }
    }
    close(client_fd);
}
