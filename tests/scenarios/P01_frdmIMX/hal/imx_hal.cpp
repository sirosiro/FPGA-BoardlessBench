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
#include <stdlib.h>
#include <fstream>
#include <string>
#include <cerrno>

// Standard base addresses and sizes
constexpr unsigned long IMX8MP_GPIO1_BASE = 0x30200000;
constexpr unsigned long IMX95_GPIO1_BASE = 0x47400000;
constexpr size_t REG_SIZE = 4096;

// GPIOポートあたりの最大ピン数 (SoCハードウェア仕様)
constexpr int IMX8MP_GPIO1_MAX_PINS = 32; // i.MX 8M Plus GPIO1 ポートは最大32ピン
constexpr int IMX95_GPIO1_MAX_PINS  = 16; // i.MX 95 GPIO1 ポート (AON) は最大16ピン

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
  const int max_pins_;                 // このポートがサポートする最大ピン数 (SoCハードウェア制限)
  std::vector<IGpioController::Direction> pin_directions_; // 初期化されたピン方向設定の保持用配列
  std::vector<bool> pin_registered_;                       // ピン初期化（登録）フラグ配列
  volatile uint16_t wdog_heartbeat_;                       // 疑似WDT生存ハートビート用
  std::vector<std::pair<int, int>> interlock_pairs_;       // 相互排他ペアリスト
  uint32_t interlocked_pins_mask_;                         // インターロック対象ピンの32bitビットマスク

  // 物理境界および方向の整合性を述語(Predicate)ラムダ式で検証する Fail-Fast テンプレートメソッド
  template <typename Predicate>
  void validatePin(int pin_num, Predicate&& validator, const char* error_reason) const {
    // 1. 物理的な境界値チェック (0 〜 max_pins_ - 1)
    if (pin_num < 0 || pin_num >= max_pins_) {
      triggerFatalError(pin_num, 
          "Pin number is out of range (Allowed: 0-" + std::to_string(max_pins_ - 1) + ").");
    }

    // 2. 登録状態と方向の取得
    bool is_registered = pin_registered_[pin_num];
    IGpioController::Direction dir = pin_directions_[pin_num];

    // 3. 外部から渡された条件式（ラムダ）による検証
    if (!validator(is_registered, dir)) {
      triggerFatalError(pin_num, error_reason);
    }
  }

  // 致命的エラー発生時のアボート処理
  void triggerFatalError(int pin_num, const std::string& details) const {
    fprintf(stderr, 
            "\n[FATAL HAL ERROR] GPIO Pin %d failed validation.\n"
            "Reason: %s\n"
            "System is aborting immediately to prevent potential hardware/board damage.\n\n",
            pin_num, details.c_str());
    if (gpio_regs_) {
      gpio_regs_[1] = 0x0; // 安全のため全ピンを入力化
    }
    abort();
  }

  // ソフトウェア・デバウンスを伴うGPIO変化監視（擬似割り込み）用のコンテキスト構造体
  struct InterruptHandler {
    int pin_num;                             // 監視対象 of GPIOピン番号
    std::function<void(int, bool)> callback; // 状態遷移確定時に実行されるコールバック関数
    bool last_stable_state;                  // デバウンス時間（20ms）安定して確定した最後のピン状態 (HIGH/LOW)
    bool last_raw_state;                     // ポーリングで読み取った直近の生のピン状態
    std::chrono::steady_clock::time_point last_change_time; // 生のピン状態に変化が発生した最後のタイムスタンプ
    bool use_filter;                         // デジタルノイズフィルタ（Glitch Filter）を使用するかどうか
    int filter_counter;                      // デジタルノイズフィルタの連続一致判定用カウンタ
  };

  std::vector<InterruptHandler>
      interrupt_handlers_; // 登録された割り込みハンドラの一覧
  std::thread
      watch_thread_; // バックグラウンドでピン状態をポーリング監視するスレッド
  std::atomic<bool> thread_running_{
      false}; // 監視スレッドの実行ループを制御するアトミックフラグ
  std::mutex
      handlers_mutex_; // ハンドラリストの登録・削除およびスレッド間データ競合を防ぐミューテックス

  // ガードチェックを一切行わない、内部用の高速な生レジスタ読み込みメソッド (インライン展開用)
  inline bool readPinRaw(int pin_num) const {
    return (gpio_regs_[0] & (1 << pin_num)) != 0;
  }

  // ガードチェックを一切行わない、内部用の高速な生レジスタ書き込みメソッド (インライン展開用)
  inline void writePinRaw(int pin_num, bool value) {
    uint32_t dr = gpio_regs_[0];
    if (value) {
      dr |= (1 << pin_num);
    } else {
      dr &= ~(1 << pin_num);
    }
    gpio_regs_[0] = dr;
  }

  // 登録されたすべてのピンを単一のスレッドで一括監視（イベントマルチプレクシング）するループ。
  // ピンごとに個別にスレッドを生成する設計を避け、コンテキストスイッチのオーバーヘッドとシステムリソース消費を最小限に抑えます。
  void watchLoop() {
    constexpr auto poll_interval = std::chrono::milliseconds(5);

    while (thread_running_) {
      std::this_thread::sleep_for(poll_interval);

      std::lock_guard<std::mutex> lock(
          handlers_mutex_); // RAIIによりスコープ（イテレーション）脱出時に自動unlockされる
      if (!gpio_regs_)
        continue;

      // 疑似WDTキック処理 (交互に生存シグナルを書き込む)
      wdog_heartbeat_ = (wdog_heartbeat_ == 0x5555) ? 0xAAAA : 0x5555;

      for (auto &handler : interrupt_handlers_) {
        // 生のピン状態を読み取る (内部監視ループでは検証済みのピンしか走査しないため、ガードなしのRawアクセスを使用)
        bool current_raw_state = readPinRaw(handler.pin_num);

        if (handler.use_filter) {
          // デジタルノイズフィルタ（Glitch Filter）:
          // 5msポーリングで4回連続して同じ変化後状態が安定検知されたら変化確定
          if (current_raw_state != handler.last_stable_state) {
            if (current_raw_state == handler.last_raw_state) {
              handler.filter_counter++;
              if (handler.filter_counter >= 4) { // 5ms * 4 = 20ms
                handler.last_stable_state = current_raw_state;
                handler.filter_counter = 0;
                if (handler.callback) {
                  handler.callback(handler.pin_num, handler.last_stable_state);
                }
              }
            } else {
              handler.last_raw_state = current_raw_state;
              handler.filter_counter = 0;
            }
          } else {
            handler.filter_counter = 0;
            handler.last_raw_state = current_raw_state;
          }
        } else {
          // 低遅延応答モード（デバウンスなし、即時応答）
          if (current_raw_state != handler.last_stable_state) {
            handler.last_stable_state = current_raw_state;
            handler.last_raw_state = current_raw_state;
            if (handler.callback) {
              handler.callback(handler.pin_num, handler.last_stable_state);
            }
          }
        }
      }
    }
  }

public:
  GpioControllerBase(unsigned long phys_addr, int max_pins)
      : phys_addr_(phys_addr),
        mem_fd_(-1),
        gpio_regs_(nullptr),
        max_pins_(max_pins),
        pin_directions_(max_pins, IGpioController::Direction::INPUT),
        pin_registered_(max_pins, false),
        wdog_heartbeat_(0),
        interlocked_pins_mask_(0) {}

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
    // 初期設定されたすべてのピンの境界チェック (登録前なので常にtrueを返すラムダを使用)
    for (const auto &[pin_num, dir] : pin_config) {
      validatePin(pin_num, [](bool, IGpioController::Direction) { return true; }, "");
    }
    // ピンの方向と登録フラグを配列に設定
    for (const auto &[pin_num, dir] : pin_config) {
      pin_directions_[pin_num] = dir;
      pin_registered_[pin_num] = true;
    }
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
    validatePin(pin_num, 
                [](bool is_reg, IGpioController::Direction) { return is_reg; }, 
                "readPin(): Pin was not declared in the initialization map.");
    if (!gpio_regs_)
      return false;
    // DR (Data Register) is at offset 0x00 (index 0)
    uint32_t dr = gpio_regs_[0];
    return (dr & (1 << pin_num)) != 0;
  }

  void writePin(int pin_num, IGpioController::PinState state, IGpioController::Verification verify = IGpioController::Verification::Disable) override {
    validatePin(pin_num, 
                [](bool is_reg, IGpioController::Direction dir) { 
                    return is_reg && dir == IGpioController::Direction::OUTPUT; 
                }, 
                "writePin(): Pin direction mismatch: expected OUTPUT pin.");
    
    // インターロック登録されているピンの通常の writePin による誤操作を防止
    if (interlocked_pins_mask_ & (1 << pin_num)) {
      triggerFatalError(pin_num, "writePin(): Cannot write to an interlocked pin using normal writePin. Use writePinIL instead.");
    }

    bool value = (state == IGpioController::PinState::High);
    writePinRaw(pin_num, value);

    // 書き込み確認オプション
    if (verify == IGpioController::Verification::Enable) {
      if (readPinRaw(pin_num) != value) {
        triggerFatalError(pin_num, "writePin(): Write verification failed.");
      }
    }
  }

  void writePinIL(int pin_num, IGpioController::PinState state, IGpioController::Verification verify = IGpioController::Verification::Disable) override {
    validatePin(pin_num, 
                [](bool is_reg, IGpioController::Direction dir) { 
                    return is_reg && dir == IGpioController::Direction::OUTPUT; 
                }, 
                "writePinIL(): Pin direction mismatch: expected OUTPUT pin.");
    
    // インターロックペアとして登録されているかマスクで最速確認
    if (!(interlocked_pins_mask_ & (1 << pin_num))) {
      triggerFatalError(pin_num, "writePinIL(): Pin is not registered as an interlock pair. Use registerInterlockPair first.");
    }

    bool value = (state == IGpioController::PinState::High);
    // 衝突チェック (ONにしようとする場合)
    if (value) {
      std::lock_guard<std::mutex> lock(handlers_mutex_);
      for (const auto& pair : interlock_pairs_) {
        if (pair.first == pin_num) {
          if (readPinRaw(pair.second)) {
            // 相手がすでにONの場合、両ピンを安全状態(OFF)にしてアボート
            writePinRaw(pair.first, false);
            writePinRaw(pair.second, false);
            triggerFatalError(pin_num, "writePinIL(): Interlock conflict detected! Mutually exclusive pin is already ON.");
          }
        } else if (pair.second == pin_num) {
          if (readPinRaw(pair.first)) {
            // 相手がすでにONの場合、両ピンを安全状態(OFF)にしてアボート
            writePinRaw(pair.first, false);
            writePinRaw(pair.second, false);
            triggerFatalError(pin_num, "writePinIL(): Interlock conflict detected! Mutually exclusive pin is already ON.");
          }
        }
      }
    }

    writePinRaw(pin_num, value);

    // 書き込み確認オプション
    if (verify == IGpioController::Verification::Enable) {
      if (readPinRaw(pin_num) != value) {
        triggerFatalError(pin_num, "writePinIL(): Write verification failed.");
      }
    }
  }

  void registerInterrupt(int pin_num,
                         std::function<void(int, bool)> callback,
                         bool use_filter = false) override {
    validatePin(pin_num, 
                [](bool is_reg, IGpioController::Direction dir) { 
                    return is_reg && dir == IGpioController::Direction::INPUT; 
                }, 
                "registerInterrupt(): Pin direction mismatch: expected INPUT pin.");
    std::lock_guard<std::mutex> lock(handlers_mutex_);

    bool exists = false;
    bool initial_state = readPin(pin_num);
    for (auto &handler : interrupt_handlers_) {
      if (handler.pin_num == pin_num) {
        handler.callback = callback;
        handler.last_stable_state = initial_state;
        handler.last_raw_state = initial_state;
        handler.last_change_time = std::chrono::steady_clock::now();
        handler.use_filter = use_filter;
        handler.filter_counter = 0;
        exists = true;
        break;
      }
    }

    if (!exists) {
      interrupt_handlers_.push_back({pin_num, callback, initial_state,
                                     initial_state,
                                     std::chrono::steady_clock::now(),
                                     use_filter, 0});
    }

    // 初めて割り込みハンドラが登録された際に、監視スレッドを遅延起動(Lazy Start)する
    if (!thread_running_) {
      thread_running_ = true;
      watch_thread_ = std::thread(&GpioControllerBase::watchLoop, this);
    }
  }

  volatile uint32_t* getRawRegisterAddress() override {
    return gpio_regs_;
  }

  void registerInterlockPair(int pin_a, int pin_b) override {
    validatePin(pin_a, 
                [](bool is_reg, IGpioController::Direction dir) { 
                    return is_reg && dir == IGpioController::Direction::OUTPUT; 
                }, 
                "registerInterlockPair(): Pin A must be registered as OUTPUT.");
    validatePin(pin_b, 
                [](bool is_reg, IGpioController::Direction dir) { 
                    return is_reg && dir == IGpioController::Direction::OUTPUT; 
                }, 
                "registerInterlockPair(): Pin B must be registered as OUTPUT.");

    std::lock_guard<std::mutex> lock(handlers_mutex_);
    interlock_pairs_.push_back({pin_a, pin_b});
    interlocked_pins_mask_ |= (1 << pin_a) | (1 << pin_b);
  }
};

class Imx8mpGpioController : public GpioControllerBase {
public:
  Imx8mpGpioController() : GpioControllerBase(IMX8MP_GPIO1_BASE, IMX8MP_GPIO1_MAX_PINS) {}
};

class Imx95RgpioController : public GpioControllerBase {
public:
  Imx95RgpioController() : GpioControllerBase(IMX95_GPIO1_BASE, IMX95_GPIO1_MAX_PINS) {}
};

// =============================================================================
// HalFactory implementation
// =============================================================================
SocType HalFactory::detectSocType() {
  // 1. デバイスツリーの compatible 情報を読み込んで SoC を自動判別
  int fd = ::open("/sys/firmware/devicetree/base/compatible", O_RDONLY);
  if (fd != -1) {
    char buf[256];
    memset(buf, 0, sizeof(buf));
    ssize_t bytes_read = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);

    if (bytes_read > 0) {
      // ヌル文字区切りを std::string の find でスキャンするために文字列オブジェクト化
      std::string content(buf, bytes_read);
      if (content.find("imx95") != std::string::npos) {
        return SocType::IMX95;
      }
      if (content.find("imx8mp") != std::string::npos) {
        return SocType::IMX8MP;
      }
    }
  }

  // 2. [フォールバック] 環境変数による明示的な指定をチェック (デバッグ・シミュレーション支援用)
  const char* env_soc = std::getenv("FBB_SOC_TYPE");
  if (env_soc != nullptr) {
    std::string soc_str(env_soc);
    if (soc_str == "imx8mp" || soc_str == "IMX8MP") {
      return SocType::IMX8MP;
    }
    return SocType::IMX95;
  }

  // 3. [最終フォールバック] 安全のためデフォルト値を返す
  fprintf(stderr, "[HAL WARNING] Failed to detect SoC type from device tree. Defaulting to i.MX95 context.\n");
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
