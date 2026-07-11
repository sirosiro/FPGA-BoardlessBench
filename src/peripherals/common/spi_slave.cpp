#include "spi_slave.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>

SpiSlave::SpiSlave(uint8_t cs) : cs_(cs) {}

SpiSlave::~SpiSlave() {
    stop();
}

bool SpiSlave::start(const std::string& socket_path) {
    socket_path_ = socket_path;
    running_ = true;

    // 1. UNIXドメインソケットの作成
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        std::cerr << "[SpiSlave] Error: Failed to create socket\n";
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
        std::cerr << "[SpiSlave] Error: Failed to bind socket to " << socket_path_ << "\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // 3. 待ち受け開始
    if (listen(server_fd_, 5) == -1) {
        std::cerr << "[SpiSlave] Error: Failed to listen on socket\n";
        stop();
        return false;
    }

    std::cout << "[SpiSlave] Daemon listening on " << socket_path_ << "\n";
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

void SpiSlave::stop() {
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

void SpiSlave::handleClient(int client_fd) {
    while (running_) {
        uint16_t len = 0;

        // 1. SPI転送データ長 (2バイト) の受信
        if (recv(client_fd, &len, sizeof(len), 0) <= 0) break;

        // 2. マスタからの送信データ (tx_data) の受信
        std::vector<uint8_t> tx_data(len);
        if (len > 0) {
            ssize_t received = 0;
            while (received < len) {
                ssize_t r = recv(client_fd, tx_data.data() + received, len - received, 0);
                if (r <= 0) break;
                received += r;
            }
            if (received < len) break; // 通信切断
        }

        // 3. 仮想ペリフェラルの全二重転送イベントの処理
        std::vector<uint8_t> rx_data = onTransfer(tx_data);

        // 安全対策：応答サイズが転送サイズと異なる場合はサイズを補正
        if (rx_data.size() != len) {
            rx_data.resize(len, 0);
        }

        // 4. 同期した応答データ (rx_data) の返送
        if (len > 0) {
            send(client_fd, rx_data.data(), len, 0);
        }
    }
    close(client_fd);
}
