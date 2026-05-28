#include "imx_hal.hpp"
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// Standard base addresses and sizes
constexpr unsigned long IMX8MP_GPIO1_BASE = 0x30200000;
constexpr unsigned long IMX95_GPIO1_BASE = 0x47400000;
constexpr size_t REG_SIZE = 4096;

// =============================================================================
// UART Controllers (POSIX implementation)
// =============================================================================
class PosixUartController : public ISerialPort {
private:
  int fd_;

public:
  PosixUartController() : fd_(-1) {}
  virtual ~PosixUartController() { close(); }

  bool open(const std::string &device_path) override {
    fd_ = ::open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ == -1) {
      perror(("Failed to open UART device: " + device_path).c_str());
      return false;
    }
    return true;
  }

  void close() override {
    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  int read(uint8_t *buf, size_t len) override {
    if (fd_ == -1)
      return -1;
    return ::read(fd_, buf, len);
  }

  int write(const uint8_t *buf, size_t len) override {
    if (fd_ == -1)
      return -1;
    return ::write(fd_, buf, len);
  }
};

class MxcUartController : public PosixUartController {};
class LpUartController : public PosixUartController {};

// =============================================================================
// GPIO Controllers (MMIO via /dev/mem)
// =============================================================================
class GpioControllerBase : public IGpioController {
protected:
    unsigned long phys_addr_;            // GPIOコントローラのベース物理アドレス
    int mem_fd_;                         // /dev/mem アクセス用のファイル記述子
    volatile uint32_t* gpio_regs_;       // mmapされたレジスタ群の仮想メモリアドレスポインタ (最適化防止のためvolatile指定)

    // ソフトウェア・デバウンスを伴うGPIO変化監視（擬似割り込み）用のコンテキスト構造体
    struct InterruptHandler {
        int pin_num;                             // 監視対象のGPIOピン番号
        std::function<void(int, bool)> callback; // 状態遷移確定時に実行されるコールバック関数
        bool last_stable_state;                  // デバウンス時間（20ms）安定して確定した最後のピン状態 (HIGH/LOW)
        bool last_raw_state;                     // ポーリングで読み取った直近の生のピン状態
        std::chrono::steady_clock::time_point last_change_time; // 生のピン状態に変化が発生した最後のタイムスタンプ
    };

    std::vector<InterruptHandler> interrupt_handlers_; // 登録された割り込みハンドラの一覧
    std::thread watch_thread_;                         // バックグラウンドでピン状態をポーリング監視するスレッド
    std::atomic<bool> thread_running_{false};          // 監視スレッドの実行ループを制御するアトミックフラグ
    std::mutex handlers_mutex_;                        // ハンドラリストの登録・削除およびスレッド間データ競合を防ぐミューテックス

  // 登録されたすべてのピンを単一のスレッドで一括監視（イベントマルチプレクシング）するループ。
  // ピンごとに個別にスレッドを生成する設計を避け、コンテキストスイッチのオーバーヘッドとシステムリソース消費を最小限に抑えます。
  void watchLoop() {
    constexpr auto poll_interval = std::chrono::milliseconds(5);
    constexpr auto debounce_time = std::chrono::milliseconds(20);

    while (thread_running_) {
      std::this_thread::sleep_for(poll_interval);

      std::lock_guard<std::mutex> lock(
          handlers_mutex_); // RAIIによりスコープ（イテレーション）脱出時に自動unlockされる
      if (!gpio_regs_)
        continue;

      auto now = std::chrono::steady_clock::now();
      for (auto &handler : interrupt_handlers_) {
        // 生のピン状態を読み取る
        bool current_raw_state = readPin(handler.pin_num);

        if (current_raw_state != handler.last_raw_state) {
          // 生の値が変化した場合：チャタリングの最中か新たな変化の始まりなので、タイマーをリセットする
          handler.last_raw_state = current_raw_state;
          handler.last_change_time = now;
        } else if (current_raw_state != handler.last_stable_state) {
          // 生の値は前回から安定しているが、確定済みの「安定状態」とは異なる状態が続いている場合：
          // 設定されたデバウンス時間 (20ms)
          // 以上同じ値が維持されていれば、変化確定とみなす
          if (now - handler.last_change_time >= debounce_time) {
            handler.last_stable_state = current_raw_state;
            if (handler.callback) {
              handler.callback(handler.pin_num, handler.last_stable_state);
            }
          }
        }
      }
    }
  }

public:
  GpioControllerBase(unsigned long phys_addr)
      : phys_addr_(phys_addr), mem_fd_(-1), gpio_regs_(nullptr) {}

  virtual ~GpioControllerBase() {
    // 監視スレッドの実行ループを終了させる
    thread_running_ = false;
    // スレッドが終了するまで同期して待機する (ダングリングスレッドの防止)
    if (watch_thread_.joinable()) {
      watch_thread_.join();
    }
    // mmapされたメモリ空間のアンマップとファイル記述子のクローズを行う
    close_mem();
  }

  bool
  init(const std::unordered_map<int, IGpioController::Direction> &pin_config) {
    mem_fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd_ == -1) {
      perror("[HAL GpioController] Failed to open /dev/mem");
      return false;
    }
    void *virt_base = mmap(NULL, REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                           mem_fd_, phys_addr_);
    if (virt_base == MAP_FAILED) {
      perror("[HAL GpioController] Failed to mmap GPIO base");
      ::close(mem_fd_);
      mem_fd_ = -1;
      return false;
    }
    gpio_regs_ = static_cast<volatile uint32_t *>(virt_base);

    // ピン方向の一括設定 (1 = output, 0 = input)
    for (const auto &[pin_num, dir] : pin_config) {
      uint32_t gdir = gpio_regs_[1];
      if (dir == IGpioController::Direction::OUTPUT) {
        gdir |= (1 << pin_num);
      } else {
        gdir &= ~(1 << pin_num);
      }
      gpio_regs_[1] = gdir;
    }

    return true;
  }

  void close_mem() {
    if (gpio_regs_ && gpio_regs_ != MAP_FAILED) {
      munmap(const_cast<uint32_t *>(gpio_regs_), REG_SIZE);
      gpio_regs_ = nullptr;
    }
    if (mem_fd_ != -1) {
      ::close(mem_fd_);
      mem_fd_ = -1;
    }
  }

  bool readPin(int pin_num) override {
    if (!gpio_regs_)
      return false;
    // DR (Data Register) is at offset 0x00 (index 0)
    uint32_t dr = gpio_regs_[0];
    return (dr & (1 << pin_num)) != 0;
  }

  void writePin(int pin_num, bool value) override {
    if (!gpio_regs_)
      return;
    // DR (Data Register) is at offset 0x00 (index 0)
    uint32_t dr = gpio_regs_[0];
    if (value) {
      dr |= (1 << pin_num);
    } else {
      dr &= ~(1 << pin_num);
    }
    gpio_regs_[0] = dr;
  }

  void registerInterrupt(int pin_num,
                         std::function<void(int, bool)> callback) override {
    std::lock_guard<std::mutex> lock(handlers_mutex_);

    bool exists = false;
    bool initial_state = readPin(pin_num);
    for (auto &handler : interrupt_handlers_) {
      if (handler.pin_num == pin_num) {
        handler.callback = callback;
        handler.last_stable_state = initial_state;
        handler.last_raw_state = initial_state;
        handler.last_change_time = std::chrono::steady_clock::now();
        exists = true;
        break;
      }
    }

    if (!exists) {
      interrupt_handlers_.push_back({pin_num, callback, initial_state,
                                     initial_state,
                                     std::chrono::steady_clock::now()});
    }

    // 初めて割り込みハンドラが登録された際に、監視スレッドを遅延起動(Lazy
    // Start)する
    if (!thread_running_) {
      thread_running_ = true;
      watch_thread_ = std::thread(&GpioControllerBase::watchLoop, this);
    }
  }
};

class Imx8mpGpioController : public GpioControllerBase {
public:
  Imx8mpGpioController() : GpioControllerBase(IMX8MP_GPIO1_BASE) {}
};

class Imx95RgpioController : public GpioControllerBase {
public:
  Imx95RgpioController() : GpioControllerBase(IMX95_GPIO1_BASE) {}
};

// =============================================================================
// HalFactory implementation
// =============================================================================
SocType HalFactory::detectSocType() {
  // Detect which device is present by trying to open them (since access() is
  // not intercepted by F-BB shim)
  int fd = ::open("/dev/ttyLP0", O_RDONLY | O_NOCTTY | O_NDELAY);
  if (fd != -1) {
    ::close(fd);
    return SocType::IMX95;
  }
  fd = ::open("/dev/ttymxc0", O_RDONLY | O_NOCTTY | O_NDELAY);
  if (fd != -1) {
    ::close(fd);
    return SocType::IMX8MP;
  }
  // Default to i.MX95 context
  return SocType::IMX95;
}

std::string HalFactory::getDefaultUartPath(SocType soc_type) {
  if (soc_type == SocType::IMX8MP) {
    return "/dev/ttymxc0";
  }
  return "/dev/ttyLP0";
}

std::unique_ptr<ISerialPort>
HalFactory::createSerialPort(SocType soc_type, const std::string &path) {
  std::string actual_path = path;
  if (actual_path.empty()) {
    actual_path = getDefaultUartPath(soc_type);
  }

  std::unique_ptr<PosixUartController> uart;
  if (soc_type == SocType::IMX8MP) {
    uart = std::make_unique<MxcUartController>();
  } else {
    uart = std::make_unique<LpUartController>();
  }

  if (uart->open(actual_path)) {
    return uart;
  }
  return nullptr;
}

std::unique_ptr<IGpioController> HalFactory::createGpioController(
    SocType soc_type,
    const std::unordered_map<int, IGpioController::Direction> &pin_config) {
  if (soc_type == SocType::IMX8MP) {
    auto gpio = std::make_unique<Imx8mpGpioController>();
    if (gpio->init(pin_config)) {
      return gpio;
    }
  } else {
    auto gpio = std::make_unique<Imx95RgpioController>();
    if (gpio->init(pin_config)) {
      return gpio;
    }
  }
  return nullptr;
}
