#include "../common/i2c_slave.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <csignal>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

constexpr size_t DISPLAY_WIDTH = 128;
constexpr size_t DISPLAY_HEIGHT = 64;
constexpr size_t GDDRAM_SIZE = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8; // 1024 bytes

class I2cSsd1306 : public I2cSlave {
public:
    I2cSsd1306(uint8_t dev_addr)
        : I2cSlave(dev_addr),
          gddram_(GDDRAM_SIZE, 0),
          shm_data_(nullptr),
          shm_fd_(-1)
    {
        // 共有メモリ (/fbb_display_0) のオープンとマッピング
        shm_fd_ = shm_open("/fbb_display_0", O_RDWR | O_CREAT, 0666);
        if (shm_fd_ != -1) {
            if (ftruncate(shm_fd_, GDDRAM_SIZE) != -1) {
                void* ptr = mmap(nullptr, GDDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
                if (ptr != MAP_FAILED) {
                    shm_data_ = static_cast<uint8_t*>(ptr);
                    std::memset(shm_data_, 0, GDDRAM_SIZE);
                    std::cout << "[SSD1306] Shared memory mapped successfully at /dev/shm/fbb_display_0\n";
                }
            }
        }
        if (!shm_data_) {
            std::cerr << "[SSD1306] Warning: Failed to map shared memory. Falling back to local buffer.\n";
        }
    }

    ~I2cSsd1306() override {
        if (shm_data_) {
            munmap(shm_data_, GDDRAM_SIZE);
        }
        if (shm_fd_ != -1) {
            close(shm_fd_);
            shm_unlink("/fbb_display_0");
        }
    }

protected:
    void onWrite(const std::vector<uint8_t>& data) override {
        if (data.empty()) return;

        uint8_t control_byte = data[0];
        bool is_data = (control_byte & 0x40) != 0;

        if (is_data) {
            std::cout << "[SSD1306] Received pixel data write of size: " << (data.size() - 1) << " bytes\n";
            // ピクセルデータの書き込み
            for (size_t i = 1; i < data.size(); ++i) {
                write_data_byte(data[i]);
            }
            sync_shm();
        } else {
            // コマンドの解析
            // SSD1306は複数コマンドを連続して送ることが可能
            size_t idx = 1;
            while (idx < data.size()) {
                parse_command(data, idx);
            }
        }
    }

    std::vector<uint8_t> onRead(size_t length) override {
        // 通常、SSD1306のI2C読み込みはダミー応答
        return std::vector<uint8_t>(length, 0x00);
    }

private:
    void write_data_byte(uint8_t val) {
        size_t ram_addr = 0;
        if (addressing_mode_ == 0x02) { // Page Addressing Mode
            ram_addr = (page_start_ * DISPLAY_WIDTH) + col_start_;
            if (ram_addr < GDDRAM_SIZE) {
                gddram_[ram_addr] = val;
            }
            // 列アドレスのみインクリメント
            col_start_++;
            if (col_start_ > col_end_) {
                col_start_ = 0; // 開始列に戻る (ページアドレスは維持)
            }
        } else if (addressing_mode_ == 0x00) { // Horizontal Addressing Mode
            ram_addr = (page_start_ * DISPLAY_WIDTH) + col_start_;
            if (ram_addr < GDDRAM_SIZE) {
                gddram_[ram_addr] = val;
            }
            col_start_++;
            if (col_start_ > col_end_) {
                col_start_ = 0;
                page_start_++;
                if (page_start_ > page_end_) {
                    page_start_ = 0;
                }
            }
        } else if (addressing_mode_ == 0x01) { // Vertical Addressing Mode
            ram_addr = (page_start_ * DISPLAY_WIDTH) + col_start_;
            if (ram_addr < GDDRAM_SIZE) {
                gddram_[ram_addr] = val;
            }
            page_start_++;
            if (page_start_ > page_end_) {
                page_start_ = 0;
                col_start_++;
                if (col_start_ > col_end_) {
                    col_start_ = 0;
                }
            }
        }
    }

    void parse_command(const std::vector<uint8_t>& data, size_t& idx) {
        if (idx >= data.size()) return;
        uint8_t cmd = data[idx++];

        // マルチバイトコマンドの処理
        if (cmd == 0x20) { // Set Memory Addressing Mode
            if (idx < data.size()) {
                addressing_mode_ = data[idx++] & 0x03;
                std::cout << "[SSD1306] Set addressing mode: " << (int)addressing_mode_ << "\n";
            }
        } else if (cmd == 0x21) { // Set Column Address Range
            if (idx + 1 < data.size()) {
                col_start_ = data[idx++];
                col_end_ = data[idx++];
                std::cout << "[SSD1306] Set Column: " << (int)col_start_ << " to " << (int)col_end_ << "\n";
            }
        } else if (cmd == 0x22) { // Set Page Address Range
            if (idx + 1 < data.size()) {
                page_start_ = data[idx++];
                page_end_ = data[idx++];
                std::cout << "[SSD1306] Set Page: " << (int)page_start_ << " to " << (int)page_end_ << "\n";
            }
        } else if (cmd == 0x81) { // Set Contrast Control
            if (idx < data.size()) {
                contrast_ = data[idx++];
            }
        } else if (cmd >= 0xB0 && cmd <= 0xB7) { // Set Page Start Address for Page Addressing Mode
            page_start_ = cmd & 0x07;
        } else if ((cmd & 0xF0) == 0x00) { // Set Lower Column Start Address for Page Addressing Mode
            col_start_ = (col_start_ & 0xF0) | (cmd & 0x0F);
        } else if ((cmd & 0xF0) == 0x10) { // Set Higher Column Start Address for Page Addressing Mode
            col_start_ = (col_start_ & 0x0F) | ((cmd & 0x0F) << 4);
        } else if (cmd == 0x8D) { // Charge Pump Setting (2バイト消費)
            if (idx < data.size()) idx++;
        } else if (cmd == 0xA8) { // Multiplex Ratio (2バイト消費)
            if (idx < data.size()) idx++;
        } else if (cmd == 0xD3) { // Display Offset (2バイト消費)
            if (idx < data.size()) idx++;
        } else if (cmd == 0xD5) { // Display Clock Divide Ratio (2バイト消費)
            if (idx < data.size()) idx++;
        } else if (cmd == 0xD9) { // Pre-charge Period (2バイト消費)
            if (idx < data.size()) idx++;
        } else if (cmd == 0xDA) { // COM Pins Hardware Configuration (2バイト消費)
            if (idx < data.size()) idx++;
        } else if (cmd == 0xDB) { // VCOMH Deselect Level (2バイト消費)
            if (idx < data.size()) idx++;
        }
    }

    void sync_shm() {
        if (shm_data_) {
            std::memcpy(shm_data_, gddram_.data(), GDDRAM_SIZE);
        }
    }

    std::vector<uint8_t> gddram_;
    uint8_t* shm_data_;
    int shm_fd_;

    uint8_t addressing_mode_{0x02}; // デフォルト: Page Addressing Mode
    uint8_t col_start_{0};
    uint8_t col_end_{127};
    uint8_t page_start_{0};
    uint8_t page_end_{7};
    uint8_t contrast_{0x7F};
};

static I2cSsd1306* g_ssd1306_instance = nullptr;

void handle_signal(int sig) {
    (void)sig;
    if (g_ssd1306_instance) {
        std::cout << "\n[SSD1306] Stopping daemon safely...\n";
        g_ssd1306_instance->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string sock_file;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            sock_file = argv[++i];
        }
    }
    if (sock_file.empty()) {
        std::cerr << "Usage: " << argv[0] << " --socket <socket_path>\n";
        return 1;
    }

    I2cSsd1306 ssd1306(0x3C); // SSD1306のデフォルトI2Cアドレス 0x3C
    g_ssd1306_instance = &ssd1306;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "[SSD1306] Mock daemon starting on " << sock_file << "\n";
    std::flush(std::cout);

    if (!ssd1306.start(sock_file)) {
        std::cerr << "[SSD1306] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[SSD1306] Daemon stopped.\n";
    return 0;
}
