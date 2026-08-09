#include "../../common/i2c_slave.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <csignal>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

constexpr size_t HT16K33_RAM_SIZE = 16; // 16 bytes display RAM

class I2cHt16k33 : public I2cSlave {
public:
    I2cHt16k33(uint8_t dev_addr)
        : I2cSlave(dev_addr),
          display_ram_(HT16K33_RAM_SIZE, 0),
          shm_data_(nullptr),
          shm_fd_(-1)
    {
        // 共有メモリ (/fbb_display_7seg_0) のオープンとマッピング
        shm_fd_ = shm_open("/fbb_display_7seg_0", O_RDWR | O_CREAT, 0666);
        if (shm_fd_ != -1) {
            if (ftruncate(shm_fd_, HT16K33_RAM_SIZE) != -1) {
                void* ptr = mmap(nullptr, HT16K33_RAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
                if (ptr != MAP_FAILED) {
                    shm_data_ = static_cast<uint8_t*>(ptr);
                    std::memset(shm_data_, 0, HT16K33_RAM_SIZE);
                    std::cout << "[HT16K33] Shared memory mapped successfully at /dev/shm/fbb_display_7seg_0\n";
                }
            }
        }
        if (!shm_data_) {
            std::cerr << "[HT16K33] Warning: Failed to map shared memory. Falling back to local buffer.\n";
        }
    }

    ~I2cHt16k33() override {
        if (shm_data_) {
            munmap(shm_data_, HT16K33_RAM_SIZE);
        }
        if (shm_fd_ != -1) {
            close(shm_fd_);
            shm_unlink("/fbb_display_7seg_0");
        }
    }

protected:
    void onWrite(const std::vector<uint8_t>& data) override {
        if (data.empty()) return;

        uint8_t cmd_or_ptr = data[0];

        // RAM書き込み (アドレスポインタ 0x00〜0x0F)
        if (cmd_or_ptr < HT16K33_RAM_SIZE) {
            size_t ram_addr = cmd_or_ptr;
            for (size_t i = 1; i < data.size(); ++i) {
                if (ram_addr < HT16K33_RAM_SIZE) {
                    display_ram_[ram_addr++] = data[i];
                }
            }
            sync_shm();
        } else {
            // コマンド解析
            if (cmd_or_ptr == 0x21) {
                oscillator_on_ = true;
                std::cout << "[HT16K33] System Setup: Oscillator ON\n";
            } else if (cmd_or_ptr == 0x20) {
                oscillator_on_ = false;
                std::cout << "[HT16K33] System Setup: Oscillator OFF\n";
            } else if ((cmd_or_ptr & 0xF0) == 0x80) {
                display_on_ = (cmd_or_ptr & 0x01) != 0;
                blink_rate_ = (cmd_or_ptr >> 1) & 0x03; // 0=off, 1=2Hz, 2=1Hz, 3=0.5Hz
                std::cout << "[HT16K33] Display Setup: ON=" << display_on_ << ", BlinkRate=" << (int)blink_rate_ << "\n";
            } else if ((cmd_or_ptr & 0xF0) == 0xE0) {
                brightness_ = cmd_or_ptr & 0x0F;
                std::cout << "[HT16K33] Dimming: Brightness=" << (int)brightness_ << "\n";
            }
        }
    }

    std::vector<uint8_t> onRead(size_t length) override {
        std::vector<uint8_t> resp(length, 0x00);
        for (size_t i = 0; i < length && i < HT16K33_RAM_SIZE; ++i) {
            resp[i] = display_ram_[i];
        }
        return resp;
    }

private:
    void sync_shm() {
        if (shm_data_) {
            std::memcpy(shm_data_, display_ram_.data(), HT16K33_RAM_SIZE);
        }
    }

    std::vector<uint8_t> display_ram_;
    uint8_t* shm_data_;
    int shm_fd_;

    bool oscillator_on_{false};
    bool display_on_{true};
    uint8_t blink_rate_{0};
    uint8_t brightness_{15};
};

static I2cHt16k33* g_ht16k33_instance = nullptr;

void handle_signal(int sig) {
    (void)sig;
    if (g_ht16k33_instance) {
        std::cout << "\n[HT16K33] Stopping daemon safely...\n";
        g_ht16k33_instance->stop();
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

    I2cHt16k33 ht16k33(0x70); // HT16K33のデフォルトI2Cアドレス 0x70
    g_ht16k33_instance = &ht16k33;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "[HT16K33] Mock daemon starting on " << sock_file << "\n";
    std::flush(std::cout);

    if (!ht16k33.start(sock_file)) {
        std::cerr << "[HT16K33] Failed to start daemon.\n";
        return 1;
    }

    std::cout << "[HT16K33] Daemon stopped.\n";
    return 0;
}
