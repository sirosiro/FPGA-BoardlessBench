#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <memory>
#include <iomanip>

/**
 * @brief 物理アドレス空間を抽象化する基底クラス
 */
class MemoryMappedDevice {
public:
    virtual ~MemoryMappedDevice() = default;
    virtual void write32(uint32_t offset, uint32_t value) = 0;
    virtual uint32_t read32(uint32_t offset) const = 0;
};

/**
 * @brief UIOデバイスを介した実際のメモリアクセス実装
 */
class UioDevice : public MemoryMappedDevice {
private:
    int fd;
    void* base;
    size_t size;

public:
    UioDevice(const std::string& path, size_t size) : size(size) {
        fd = open(path.c_str(), O_RDWR | O_SYNC);
        if (fd < 0) throw std::runtime_error("Failed to open " + path);
        base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) throw std::runtime_error("mmap failed");
    }

    ~UioDevice() {
        if (base != MAP_FAILED) munmap(base, size);
        if (fd >= 0) close(fd);
    }

    void write32(uint32_t offset, uint32_t value) override {
        *((volatile uint32_t*)((uint8_t*)base + offset)) = value;
    }

    uint32_t read32(uint32_t offset) const override {
        return *((volatile uint32_t*)((uint8_t*)base + offset));
    }
};

/**
 * @brief AXI GPIO 制御クラス
 */
class GpioPeripheral {
private:
    MemoryMappedDevice& dev;
    uint32_t offset;

public:
    GpioPeripheral(MemoryMappedDevice& d, uint32_t off) : dev(d), offset(off) {}
    
    void set_direction(uint32_t mask) { dev.write32(offset + 4, mask); }
    void write_data(uint32_t val) { dev.write32(offset + 0, val); }
    uint32_t read_data() { return dev.read32(offset + 0); }
};

/**
 * @brief カスタムパターンエンジン制御クラス
 */
class LfsrEngine {
private:
    MemoryMappedDevice& dev;
    uint32_t offset;

public:
    LfsrEngine(MemoryMappedDevice& d, uint32_t off) : dev(d), offset(off) {}

    enum class Mode { SEQUENTIAL = 0, BINARY = 1, LFSR = 2, OFF = 3 };

    void set_control(bool run, Mode mode) {
        uint32_t ctrl = (run ? 1 : 0) | (static_cast<uint32_t>(mode) << 1);
        dev.write32(offset + 0, ctrl);
    }

    void set_speed(uint32_t speed_val) {
        // Higher value = Slower speed
        // Calibrated to simulator's ~10kHz clock (usleep(100) per cycle)
        // speed=10: ~10 ticks/sec, speed=5: ~1.7 ticks/sec, speed=1: ~1 tick/sec
        uint32_t val = (11 - speed_val) * 1000;
        dev.write32(offset + 4, val);
    }

    uint32_t get_status() { return dev.read32(offset + 8); }
};

/**
 * @brief システム全体の統合シェル
 */
class SystemShell {
private:
    GpioPeripheral& gpio;
    LfsrEngine& engine;

public:
    SystemShell(GpioPeripheral& g, LfsrEngine& e) : gpio(g), engine(e) {}

    void run() {
        std::cout << "--- F-BB S01 C++ Showcase Shell ---" << std::endl;
        std::cout << "Type 'help' for commands." << std::endl;

        std::string line;
        while (true) {
            std::cout << "> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string cmd;
            ss >> cmd;

            if (cmd == "help") {
                std::cout << "  mode [seq|bin|lfsr|off] : Change engine mode" << std::endl;
                std::cout << "  speed [1-10]            : Set pattern speed" << std::endl;
                std::cout << "  status                  : Show current engine status" << std::endl;
                std::cout << "  exit                    : Exit shell" << std::endl;
            } else if (cmd == "mode") {
                std::string mode_str;
                ss >> mode_str;
                if (mode_str == "seq") engine.set_control(true, LfsrEngine::Mode::SEQUENTIAL);
                else if (mode_str == "bin") engine.set_control(true, LfsrEngine::Mode::BINARY);
                else if (mode_str == "lfsr") engine.set_control(true, LfsrEngine::Mode::LFSR);
                else engine.set_control(false, LfsrEngine::Mode::OFF);
                std::cout << "Mode changed to " << mode_str << std::endl;
            } else if (cmd == "speed") {
                uint32_t s;
                if (ss >> s && s >= 1 && s <= 10) {
                    engine.set_speed(s);
                    std::cout << "Speed set to " << s << std::endl;
                }
            } else if (cmd == "status") {
                uint32_t val = engine.get_status();
                std::cout << "Current Engine Value: 0x" << std::hex << std::setw(8) << std::setfill('0') << val << std::dec << std::endl;
            } else if (cmd == "exit") {
                break;
            } else {
                std::cout << "Unknown command." << std::endl;
            }
        }
    }
};

#include <termios.h>

int main() {
    try {
        // UARTデバイスをオープンして標準入出力にリダイレクト
        // これによりダッシュボードのUARTコンソールにシェルが表示される
        int uart_fd = open("/dev/ttyUL0", O_RDWR | O_NOCTTY);
        if (uart_fd >= 0) {
            struct termios options;
            tcgetattr(uart_fd, &options);
            cfsetispeed(&options, B115200);
            cfsetospeed(&options, B115200);
            options.c_cflag |= (CLOCAL | CREAD);
            options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw mode
            tcsetattr(uart_fd, TCSANOW, &options);

            dup2(uart_fd, STDIN_FILENO);
            dup2(uart_fd, STDOUT_FILENO);
            dup2(uart_fd, STDERR_FILENO);
        }

        // 各デバイスを個別のUIOファイルとしてオープン
        UioDevice uio_custom("/dev/uio0", 0x1000);
        UioDevice uio_gpio("/dev/uio1", 0x1000);

        GpioPeripheral gpio(uio_gpio, 0x0000);
        LfsrEngine engine(uio_custom, 0x0000);
        
        // GPIO を全ビット出力モードに設定 (TRI=0)
        gpio.set_direction(0x00000000);
        
        SystemShell shell(gpio, engine);
        shell.run();

        if (uart_fd >= 0) close(uart_fd);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
