#include "imx_hal.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <dlfcn.h>
#include <fcntl.h>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// OpenGL ES and EGL headers
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// GBM & DRM headers and fallbacks
#if __has_include(<gbm.h>)
#include <gbm.h>
#else
// Mock declarations for host environment compilation
struct gbm_device;
struct gbm_bo;

#define GBM_FORMAT_ARGB8888 0x34325241
#define GBM_BO_USE_SCANOUT (1 << 0)
#define GBM_BO_USE_RENDERING (1 << 1)
#endif

// Guard extension types and constants in case they are not in the current EGL headers
#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x314F
#endif
#ifndef EGL_DMA_BUF_PLANE0_FD_EXT
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3181
#endif
#ifndef EGL_DMA_BUF_PLANE0_OFFSET_EXT
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3182
#endif
#ifndef EGL_DMA_BUF_PLANE0_PITCH_EXT
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3183
#endif

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

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

// =============================================================================
// VideoProcessor implementations (OpenGL ES / EGL)
// =============================================================================

class HostGlesProcessor : public IVideoProcessor {
private:
  EGLDisplay egl_dpy_ = EGL_NO_DISPLAY;
  EGLContext egl_ctx_ = EGL_NO_CONTEXT;
  GLuint program_ = 0;
  GLuint fbo_ = 0;
  GLuint tex_in_[4] = {0, 0, 0, 0};
  GLuint tex_out_ = 0;
  CalibrationData calib_params_[4];

  GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      char infoLog[512];
      glGetShaderInfoLog(shader, 512, NULL, infoLog);
      fprintf(stderr, "[HostGlesProcessor] Shader compile error: %s\n", infoLog);
      glDeleteShader(shader);
      return 0;
    }
    return shader;
  }

public:
  HostGlesProcessor() {
    for (int i = 0; i < 4; i++) {
      calib_params_[i].distortion_k1 = 0.0f;
      calib_params_[i].distortion_k2 = 0.0f;
      memset(calib_params_[i].affine_matrix, 0, sizeof(calib_params_[i].affine_matrix));
      calib_params_[i].affine_matrix[0] = 1.0f;
      calib_params_[i].affine_matrix[5] = 1.0f;
      calib_params_[i].affine_matrix[10] = 1.0f;
      calib_params_[i].affine_matrix[15] = 1.0f;
      calib_params_[i].color_balance[0] = 1.0f;
      calib_params_[i].color_balance[1] = 1.0f;
      calib_params_[i].color_balance[2] = 1.0f;
      calib_params_[i].color_balance[3] = 1.0f; // Gamma
    }
  }
  virtual ~HostGlesProcessor() { terminate(); }

  void setCalibrationParams(int camera_index, const CalibrationData& params) override {
    if (camera_index >= 0 && camera_index < 4) {
      calib_params_[camera_index] = params;
    }
  }

  bool initialize() override {
    printf("[HAL] Initializing Mesa Surfaceless EGL...\n");

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (eglGetPlatformDisplayEXT) {
      egl_dpy_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    }

    if (egl_dpy_ == EGL_NO_DISPLAY) {
      printf("[HAL] Falling back to default eglGetDisplay...\n");
      egl_dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

    if (egl_dpy_ == EGL_NO_DISPLAY) {
      fprintf(stderr, "[HAL ERROR] Failed to get EGL display.\n");
      return false;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_dpy_, &major, &minor)) {
      fprintf(stderr, "[HAL ERROR] eglInitialize failed.\n");
      return false;
    }
    printf("[HAL] EGL version %d.%d initialized.\n", major, minor);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_DONT_CARE,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(egl_dpy_, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
      fprintf(stderr, "[HAL ERROR] eglChooseConfig failed.\n");
      return false;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl_ctx_ = eglCreateContext(egl_dpy_, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_ctx_ == EGL_NO_CONTEXT) {
      fprintf(stderr, "[HAL ERROR] eglCreateContext failed.\n");
      return false;
    }

    if (!eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_ctx_)) {
      fprintf(stderr, "[HAL ERROR] eglMakeCurrent failed.\n");
      return false;
    }

    printf("[HAL] EGL Surfaceless Context bound successfully.\n");
    printf("[HAL] GL Vendor: %s, Renderer: %s, Version: %s\n",
           glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

    const char *vs_src =
        "attribute vec4 position;\n"
        "attribute vec2 texcoord;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "  gl_Position = position;\n"
        "  v_texcoord = texcoord;\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "varying vec2 v_texcoord;\n"
        "uniform sampler2D s_tex0;\n"
        "uniform sampler2D s_tex1;\n"
        "uniform sampler2D s_tex2;\n"
        "uniform sampler2D s_tex3;\n"
        "uniform vec2 u_distortion[4];\n"
        "uniform mat4 u_affine_matrix[4];\n"
        "uniform vec4 u_color_balance[4];\n"
        "\n"
        "vec2 apply_distortion(vec2 uv, vec2 k) {\n"
        "  vec2 d = uv - vec2(0.5, 0.5);\n"
        "  float r2 = dot(d, d);\n"
        "  float f = 1.0 + k.x * r2 + k.y * r2 * r2;\n"
        "  return vec2(0.5, 0.5) + d * f;\n"
        "}\n"
        "\n"
        "vec2 apply_affine(vec2 uv, mat4 mat) {\n"
        "  vec4 pos = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, 0.0, 1.0);\n"
        "  vec4 trans = mat * pos;\n"
        "  return vec2((trans.x / trans.w + 1.0) * 0.5, (trans.y / trans.w + 1.0) * 0.5);\n"
        "}\n"
        "\n"
        "void main() {\n"
        "  vec2 uv = v_texcoord;\n"
        "  int cam_idx = 0;\n"
        "  vec2 sub_uv = vec2(0.0);\n"
        "  vec4 color = vec4(0.0);\n"
        "  if (uv.x < 0.5 && uv.y < 0.5) {\n"
        "    cam_idx = 0;\n"
        "    sub_uv = uv * 2.0;\n"
        "  } else if (uv.x >= 0.5 && uv.y < 0.5) {\n"
        "    cam_idx = 1;\n"
        "    sub_uv = vec2(uv.x - 0.5, uv.y) * 2.0;\n"
        "  } else if (uv.x < 0.5 && uv.y >= 0.5) {\n"
        "    cam_idx = 2;\n"
        "    sub_uv = vec2(uv.x, uv.y - 0.5) * 2.0;\n"
        "  } else {\n"
        "    cam_idx = 3;\n"
        "    sub_uv = (uv - vec2(0.5, 0.5)) * 2.0;\n"
        "  }\n"
        "  vec2 corr_uv = apply_affine(sub_uv, u_affine_matrix[cam_idx]);\n"
        "  corr_uv = apply_distortion(corr_uv, u_distortion[cam_idx]);\n"
        "  if (corr_uv.x < 0.0 || corr_uv.x > 1.0 || corr_uv.y < 0.0 || corr_uv.y > 1.0) {\n"
        "    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "    return;\n"
        "  }\n"
        "  if (cam_idx == 0) {\n"
        "    color = texture2D(s_tex0, corr_uv);\n"
        "  } else if (cam_idx == 1) {\n"
        "    color = texture2D(s_tex1, corr_uv);\n"
        "  } else if (cam_idx == 2) {\n"
        "    color = texture2D(s_tex2, corr_uv);\n"
        "  } else {\n"
        "    color = texture2D(s_tex3, corr_uv);\n"
        "  }\n"
        "  vec3 adjusted_rgb = pow(color.rgb * u_color_balance[cam_idx].rgb, vec3(u_color_balance[cam_idx].a));\n"
        "  gl_FragColor = vec4(1.0 - adjusted_rgb, color.a);\n"
        "}\n";

    GLuint vs = compileShader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    GLint linked;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
      char infoLog[512];
      glGetProgramInfoLog(program_, 512, NULL, infoLog);
      fprintf(stderr, "[HostGlesProcessor] Program link error: %s\n", infoLog);
      return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenTextures(4, tex_in_);
    glGenTextures(1, &tex_out_);
    glGenFramebuffers(1, &fbo_);

    return true;
  }

  bool processFrame(const uint8_t* in_frames[4], uint8_t* out_data, int width, int height) override {
    if (egl_dpy_ == EGL_NO_DISPLAY || egl_ctx_ == EGL_NO_CONTEXT) {
      fprintf(stderr, "[HAL ERROR] VideoProcessor not initialized.\n");
      return false;
    }

    for (int i = 0; i < 4; i++) {
      if (!in_frames[i]) {
        fprintf(stderr, "[HAL ERROR] Input frame %d is null.\n", i);
        return false;
      }
      glBindTexture(GL_TEXTURE_2D, tex_in_[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, in_frames[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    glBindTexture(GL_TEXTURE_2D, tex_out_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_out_, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "[HAL ERROR] Framebuffer incomplete.\n");
      return false;
    }

    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program_);

    GLint dist_loc = glGetUniformLocation(program_, "u_distortion");
    GLint affine_loc = glGetUniformLocation(program_, "u_affine_matrix");
    GLint color_loc = glGetUniformLocation(program_, "u_color_balance");

    float distortions[8];
    float color_balances[16];
    for (int i = 0; i < 4; i++) {
      distortions[i * 2] = calib_params_[i].distortion_k1;
      distortions[i * 2 + 1] = calib_params_[i].distortion_k2;
      color_balances[i * 4] = calib_params_[i].color_balance[0];
      color_balances[i * 4 + 1] = calib_params_[i].color_balance[1];
      color_balances[i * 4 + 2] = calib_params_[i].color_balance[2];
      color_balances[i * 4 + 3] = calib_params_[i].color_balance[3];
    }
    glUniform2fv(dist_loc, 4, distortions);
    glUniform4fv(color_loc, 4, color_balances);

    float matrices[64];
    for (int i = 0; i < 4; i++) {
      memcpy(&matrices[i * 16], calib_params_[i].affine_matrix, sizeof(float) * 16);
    }
    glUniformMatrix4fv(affine_loc, 4, GL_FALSE, matrices);

    GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
    };
    GLfloat texcoords[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    GLint pos_attr = glGetAttribLocation(program_, "position");
    GLint tex_attr = glGetAttribLocation(program_, "texcoord");

    glEnableVertexAttribArray(pos_attr);
    glVertexAttribPointer(pos_attr, 3, GL_FLOAT, GL_FALSE, 0, vertices);

    glEnableVertexAttribArray(tex_attr);
    glVertexAttribPointer(tex_attr, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

    for (int i = 0; i < 4; i++) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, tex_in_[i]);
      char tex_name[16];
      snprintf(tex_name, sizeof(tex_name), "s_tex%d", i);
      glUniform1i(glGetUniformLocation(program_, tex_name), i);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(pos_attr);
    glDisableVertexAttribArray(tex_attr);

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out_data);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
  }

  void terminate() override {
    if (program_) {
      glDeleteProgram(program_);
      program_ = 0;
    }
    if (fbo_) {
      glDeleteFramebuffers(1, &fbo_);
      fbo_ = 0;
    }
    for (int i = 0; i < 4; i++) {
      if (tex_in_[i]) {
        glDeleteTextures(1, &tex_in_[i]);
        tex_in_[i] = 0;
      }
    }
    if (tex_out_) {
      glDeleteTextures(1, &tex_out_);
      tex_out_ = 0;
    }
    if (egl_dpy_ != EGL_NO_DISPLAY) {
      eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (egl_ctx_ != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_dpy_, egl_ctx_);
        egl_ctx_ = EGL_NO_CONTEXT;
      }
      eglTerminate(egl_dpy_);
      egl_dpy_ = EGL_NO_DISPLAY;
    }
    printf("[HAL] Mesa Surfaceless EGL terminated.\n");
  }
};

class ImxGlesProcessorBase : public IVideoProcessor {
protected:
  SocType type_;
  EGLDisplay egl_dpy_ = EGL_NO_DISPLAY;
  EGLContext egl_ctx_ = EGL_NO_CONTEXT;
  CalibrationData calib_params_[4];

  // Extension API pointers for physical hardware
  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ = nullptr;

  // GBM library handle and dynamically loaded APIs
  void* gbm_lib_handle_ = nullptr;
  struct gbm_device* gbm_dev_ = nullptr;
  struct gbm_bo* gbm_bo_in_[4] = {nullptr, nullptr, nullptr, nullptr};
  GLuint tex_in_[4] = {0, 0, 0, 0};
  EGLImageKHR egl_img_in_[4] = {EGL_NO_IMAGE_KHR, EGL_NO_IMAGE_KHR, EGL_NO_IMAGE_KHR, EGL_NO_IMAGE_KHR};

  // Dynamically loaded GBM functions
  typedef struct gbm_device* (*PFNGBMCREATEDEVICE)(int);
  typedef void (*PFNGBMDEVICEDESTROY)(struct gbm_device*);
  typedef struct gbm_bo* (*PFNGBMBOCREATE)(struct gbm_device*, uint32_t, uint32_t, uint32_t, uint32_t);
  typedef void (*PFNGBMBODESTROY)(struct gbm_bo*);
  typedef int (*PFNGBMBOGETFD)(struct gbm_bo*);
  typedef uint32_t (*PFNGBMBOGETSTRIDE)(struct gbm_bo*);

  PFNGBMCREATEDEVICE gbm_create_device_ = nullptr;
  PFNGBMDEVICEDESTROY gbm_device_destroy_ = nullptr;
  PFNGBMBOCREATE gbm_bo_create_ = nullptr;
  PFNGBMBODESTROY gbm_bo_destroy_ = nullptr;
  PFNGBMBOGETFD gbm_bo_get_fd_ = nullptr;
  PFNGBMBOGETSTRIDE gbm_bo_get_stride_ = nullptr;

  // OpenGL ES Objects for hardware rendering
  GLuint program_ = 0;
  GLuint fbo_ = 0;
  GLuint tex_out_ = 0;

  bool loadGbmApis() {
    gbm_lib_handle_ = dlopen("libgbm.so", RTLD_LAZY);
    if (!gbm_lib_handle_) {
      gbm_lib_handle_ = dlopen("libgbm.so.1", RTLD_LAZY);
    }
    if (!gbm_lib_handle_) {
      fprintf(stderr, "[HAL WARNING] [Production] libgbm.so not found.\n");
      return false;
    }

    gbm_create_device_ = (PFNGBMCREATEDEVICE)dlsym(gbm_lib_handle_, "gbm_create_device");
    gbm_device_destroy_ = (PFNGBMDEVICEDESTROY)dlsym(gbm_lib_handle_, "gbm_device_destroy");
    gbm_bo_create_ = (PFNGBMBOCREATE)dlsym(gbm_lib_handle_, "gbm_bo_create");
    gbm_bo_destroy_ = (PFNGBMBODESTROY)dlsym(gbm_lib_handle_, "gbm_bo_destroy");
    gbm_bo_get_fd_ = (PFNGBMBOGETFD)dlsym(gbm_lib_handle_, "gbm_bo_get_fd");
    gbm_bo_get_stride_ = (PFNGBMBOGETSTRIDE)dlsym(gbm_lib_handle_, "gbm_bo_get_stride");

    if (!gbm_create_device_ || !gbm_device_destroy_ || !gbm_bo_create_ ||
        !gbm_bo_destroy_ || !gbm_bo_get_fd_ || !gbm_bo_get_stride_) {
      fprintf(stderr, "[HAL WARNING] [Production] Some GBM symbols are missing.\n");
      dlclose(gbm_lib_handle_);
      gbm_lib_handle_ = nullptr;
      return false;
    }
    return true;
  }

  GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      char infoLog[512];
      glGetShaderInfoLog(shader, 512, NULL, infoLog);
      fprintf(stderr, "[HAL ERROR] [Production] Shader compile error: %s\n", infoLog);
      glDeleteShader(shader);
      return 0;
    }
    return shader;
  }

public:
  ImxGlesProcessorBase(SocType type) : type_(type) {
    for (int i = 0; i < 4; i++) {
      calib_params_[i].distortion_k1 = 0.0f;
      calib_params_[i].distortion_k2 = 0.0f;
      memset(calib_params_[i].affine_matrix, 0, sizeof(calib_params_[i].affine_matrix));
      calib_params_[i].affine_matrix[0] = 1.0f;
      calib_params_[i].affine_matrix[5] = 1.0f;
      calib_params_[i].affine_matrix[10] = 1.0f;
      calib_params_[i].affine_matrix[15] = 1.0f;
      calib_params_[i].color_balance[0] = 1.0f;
      calib_params_[i].color_balance[1] = 1.0f;
      calib_params_[i].color_balance[2] = 1.0f;
      calib_params_[i].color_balance[3] = 1.0f; // Gamma
    }
  }
  virtual ~ImxGlesProcessorBase() { terminate(); }

  void setCalibrationParams(int camera_index, const CalibrationData& params) override {
    if (camera_index >= 0 && camera_index < 4) {
      calib_params_[camera_index] = params;
    }
  }

  bool initialize() override {
    printf("[HAL] [Production] Initializing Hardware EGL/GLES for %s...\n",
           (type_ == SocType::IMX95) ? "i.MX95 (Mali)" : "i.MX8MP (Vivante)");

    // 1. Open DRM Render Node (or Card) for GBM
    int drm_fd = -1;
#if __has_include(<gbm.h>)
    drm_fd = open("/dev/dri/renderD128", O_RDWR);
    if (drm_fd < 0) {
      drm_fd = open("/dev/dri/card0", O_RDWR);
    }
#endif

    // 2. Load GBM APIs dynamically
    if (drm_fd >= 0 && loadGbmApis()) {
      gbm_dev_ = gbm_create_device_(drm_fd);
      if (gbm_dev_) {
        printf("[HAL] [Production] GBM device created successfully via render node.\n");
      } else {
        fprintf(stderr, "[HAL WARNING] [Production] Failed to create GBM device from DRM fd.\n");
        close(drm_fd);
        drm_fd = -1;
      }
    } else {
      if (drm_fd >= 0) {
        close(drm_fd);
        drm_fd = -1;
      }
      printf("[HAL] [Production] Running without native GBM (Mock/Fallback mode enabled).\n");
    }

    // Production code path (GBM and native window initialization)
    egl_dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy_ == EGL_NO_DISPLAY) {
      fprintf(stderr, "[HAL ERROR] [Production] Failed to get EGL Display.\n");
      return false;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_dpy_, &major, &minor)) {
      return false;
    }

    // Load extensions dynamically to support standard compilation on both Mesa and Board GLES SDK
    eglCreateImageKHR_ = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR_ = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    glEGLImageTargetTexture2DOES_ = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_dpy_, config_attribs, &config, 1, &num_configs);

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_ctx_ = eglCreateContext(egl_dpy_, config, EGL_NO_CONTEXT, ctx_attribs);
    if (egl_ctx_ == EGL_NO_CONTEXT) return false;

    // bind offscreen target context
    if (!eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_ctx_)) {
      fprintf(stderr, "[HAL ERROR] [Production] eglMakeCurrent failed.\n");
      return false;
    }

    printf("[HAL] [Production] Hardware GPU context established successfully.\n");

    // Initialize shaders
    const char *vs_src =
        "attribute vec4 position;\n"
        "attribute vec2 texcoord;\n"
        "varying vec2 v_texcoord;\n"
        "void main() {\n"
        "  gl_Position = position;\n"
        "  v_texcoord = texcoord;\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "varying vec2 v_texcoord;\n"
        "uniform sampler2D s_tex0;\n"
        "uniform sampler2D s_tex1;\n"
        "uniform sampler2D s_tex2;\n"
        "uniform sampler2D s_tex3;\n"
        "uniform vec2 u_distortion[4];\n"
        "uniform mat4 u_affine_matrix[4];\n"
        "uniform vec4 u_color_balance[4];\n"
        "\n"
        "vec2 apply_distortion(vec2 uv, vec2 k) {\n"
        "  vec2 d = uv - vec2(0.5, 0.5);\n"
        "  float r2 = dot(d, d);\n"
        "  float f = 1.0 + k.x * r2 + k.y * r2 * r2;\n"
        "  return vec2(0.5, 0.5) + d * f;\n"
        "}\n"
        "\n"
        "vec2 apply_affine(vec2 uv, mat4 mat) {\n"
        "  vec4 pos = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, 0.0, 1.0);\n"
        "  vec4 trans = mat * pos;\n"
        "  return vec2((trans.x / trans.w + 1.0) * 0.5, (trans.y / trans.w + 1.0) * 0.5);\n"
        "}\n"
        "\n"
        "void main() {\n"
        "  vec2 uv = v_texcoord;\n"
        "  int cam_idx = 0;\n"
        "  vec2 sub_uv = vec2(0.0);\n"
        "  vec4 color = vec4(0.0);\n"
        "  if (uv.x < 0.5 && uv.y < 0.5) {\n"
        "    cam_idx = 0;\n"
        "    sub_uv = uv * 2.0;\n"
        "  } else if (uv.x >= 0.5 && uv.y < 0.5) {\n"
        "    cam_idx = 1;\n"
        "    sub_uv = vec2(uv.x - 0.5, uv.y) * 2.0;\n"
        "  } else if (uv.x < 0.5 && uv.y >= 0.5) {\n"
        "    cam_idx = 2;\n"
        "    sub_uv = vec2(uv.x, uv.y - 0.5) * 2.0;\n"
        "  } else {\n"
        "    cam_idx = 3;\n"
        "    sub_uv = (uv - vec2(0.5, 0.5)) * 2.0;\n"
        "  }\n"
        "  vec2 corr_uv = apply_affine(sub_uv, u_affine_matrix[cam_idx]);\n"
        "  corr_uv = apply_distortion(corr_uv, u_distortion[cam_idx]);\n"
        "  if (corr_uv.x < 0.0 || corr_uv.x > 1.0 || corr_uv.y < 0.0 || corr_uv.y > 1.0) {\n"
        "    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "    return;\n"
        "  }\n"
        "  if (cam_idx == 0) {\n"
        "    color = texture2D(s_tex0, corr_uv);\n"
        "  } else if (cam_idx == 1) {\n"
        "    color = texture2D(s_tex1, corr_uv);\n"
        "  } else if (cam_idx == 2) {\n"
        "    color = texture2D(s_tex2, corr_uv);\n"
        "  } else {\n"
        "    color = texture2D(s_tex3, corr_uv);\n"
        "  }\n"
        "  vec3 adjusted_rgb = pow(color.rgb * u_color_balance[cam_idx].rgb, vec3(u_color_balance[cam_idx].a));\n"
        "  gl_FragColor = vec4(1.0 - adjusted_rgb, color.a);\n"
        "}\n";

    GLuint vs = compileShader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    GLint linked;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
      char infoLog[512];
      glGetProgramInfoLog(program_, 512, NULL, infoLog);
      fprintf(stderr, "[HAL ERROR] [Production] Program link error: %s\n", infoLog);
      return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenTextures(1, &tex_out_);
    glGenFramebuffers(1, &fbo_);

    return true;
  }

  bool processFrame(const uint8_t* in_frames[4], uint8_t* out_data, int width, int height) override {
    printf("[HAL] [Production] GPU hardware processing frame\n");

    // Real physical hardware Zero-Copy zero-latency logic path
    if (gbm_dev_ && eglCreateImageKHR_ && glEGLImageTargetTexture2DOES_ && program_ && fbo_ && tex_out_) {
      for (int i = 0; i < 4; i++) {
        if (!in_frames[i]) continue;
        
        // Clean up previous buffer and texture resources
        if (egl_img_in_[i] != EGL_NO_IMAGE_KHR) {
          eglDestroyImageKHR_(egl_dpy_, egl_img_in_[i]);
          egl_img_in_[i] = EGL_NO_IMAGE_KHR;
        }
        if (gbm_bo_in_[i]) {
          gbm_bo_destroy_(gbm_bo_in_[i]);
          gbm_bo_in_[i] = nullptr;
        }

        // 1. Allocate buffer using GBM
        gbm_bo_in_[i] = gbm_bo_create_(gbm_dev_, width, height, GBM_FORMAT_ARGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!gbm_bo_in_[i]) {
          fprintf(stderr, "[HAL ERROR] [Production] Failed to create gbm_bo for stream %d\n", i);
          return false;
        }

        int dbuf_fd = gbm_bo_get_fd_(gbm_bo_in_[i]);
        uint32_t stride = gbm_bo_get_stride_(gbm_bo_in_[i]);

        EGLint img_attribs[] = {
            EGL_WIDTH, width,
            EGL_HEIGHT, height,
            EGL_LINUX_DMA_BUF_EXT, // format or target config
            EGL_DMA_BUF_PLANE0_FD_EXT, dbuf_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)stride,
            EGL_NONE
        };

        // 2. Create EGLImageKHR from DMA-BUF
        egl_img_in_[i] = eglCreateImageKHR_(egl_dpy_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, img_attribs);
        if (egl_img_in_[i] == EGL_NO_IMAGE_KHR) {
          fprintf(stderr, "[HAL ERROR] [Production] Failed to create EGLImage from dma-buf fd for stream %d\n", i);
          close(dbuf_fd);
          return false;
        }
        close(dbuf_fd);

        // 3. Bind to GL_TEXTURE_2D target (or GL_TEXTURE_EXTERNAL_OES on hardware)
        if (tex_in_[i] == 0) {
          glGenTextures(1, &tex_in_[i]);
        }
        glBindTexture(GL_TEXTURE_2D, tex_in_[i]);
        glEGLImageTargetTexture2DOES_(GL_TEXTURE_2D, egl_img_in_[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      }

      // Bind output texture
      glBindTexture(GL_TEXTURE_2D, tex_out_);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

      glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_out_, 0);

      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[HAL ERROR] [Production] Framebuffer incomplete.\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
      }

      glViewport(0, 0, width, height);
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      glClear(GL_COLOR_BUFFER_BIT);

      glUseProgram(program_);

      GLint dist_loc = glGetUniformLocation(program_, "u_distortion");
      GLint affine_loc = glGetUniformLocation(program_, "u_affine_matrix");
      GLint color_loc = glGetUniformLocation(program_, "u_color_balance");

      float distortions[8];
      float color_balances[16];
      for (int i = 0; i < 4; i++) {
        distortions[i * 2] = calib_params_[i].distortion_k1;
        distortions[i * 2 + 1] = calib_params_[i].distortion_k2;
        color_balances[i * 4] = calib_params_[i].color_balance[0];
        color_balances[i * 4 + 1] = calib_params_[i].color_balance[1];
        color_balances[i * 4 + 2] = calib_params_[i].color_balance[2];
        color_balances[i * 4 + 3] = calib_params_[i].color_balance[3];
      }
      glUniform2fv(dist_loc, 4, distortions);
      glUniform4fv(color_loc, 4, color_balances);

      float matrices[64];
      for (int i = 0; i < 4; i++) {
        memcpy(&matrices[i * 16], calib_params_[i].affine_matrix, sizeof(float) * 16);
      }
      glUniformMatrix4fv(affine_loc, 4, GL_FALSE, matrices);

      GLfloat vertices[] = {
          -1.0f, -1.0f, 0.0f,
           1.0f, -1.0f, 0.0f,
          -1.0f,  1.0f, 0.0f,
           1.0f,  1.0f, 0.0f,
      };
      GLfloat texcoords[] = {
          0.0f, 0.0f,
          1.0f, 0.0f,
          0.0f, 1.0f,
          1.0f, 1.0f,
      };

      GLint pos_attr = glGetAttribLocation(program_, "position");
      GLint tex_attr = glGetAttribLocation(program_, "texcoord");

      glEnableVertexAttribArray(pos_attr);
      glVertexAttribPointer(pos_attr, 3, GL_FLOAT, GL_FALSE, 0, vertices);

      glEnableVertexAttribArray(tex_attr);
      glVertexAttribPointer(tex_attr, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

      for (int i = 0; i < 4; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, tex_in_[i]);
        char tex_name[16];
        snprintf(tex_name, sizeof(tex_name), "s_tex%d", i);
        glUniform1i(glGetUniformLocation(program_, tex_name), i);
      }

      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

      glDisableVertexAttribArray(pos_attr);
      glDisableVertexAttribArray(tex_attr);

      // [Physical Board Optimization & Synchronization Guarantee]
      // Why: On real embedded GPU drivers (Vivante/Mali), glReadPixels might start executing before 
      // the GPU finishes writing to the DMA-BUF/FBO. Calling glFinish() forces the GPU command pipeline 
      // to flush and blocks the CPU thread until all rendering is complete, preventing synchronization 
      // lag and caching coherence issues on the physical board.
      // (実機GPUドライバでの、描画未完了状態でのglReadPixels開始による同期バグやキャッシュコヒーレンシ喪失を防止)
      glFinish();

      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out_data);

      glBindFramebuffer(GL_FRAMEBUFFER, 0);

      printf("[HAL] [Production] OpenGL ES hardware zero-copy pipelines loaded and bound successfully.\n");
    } else {
      // Mock fallback execution path (No native GBM)
      printf("[HAL] [Production] GBM/DMA-BUF mock fallback mode active.\n");
      if (in_frames[0]) {
        memcpy(out_data, in_frames[0], width * height * 4); // Dummy copy for non-hardware execution fallback
      }
    }

    return true;
  }

  void terminate() override {
    // Release GL program and rendering resources
    if (program_) {
      glDeleteProgram(program_);
      program_ = 0;
    }
    if (fbo_) {
      glDeleteFramebuffers(1, &fbo_);
      fbo_ = 0;
    }
    if (tex_out_) {
      glDeleteTextures(1, &tex_out_);
      tex_out_ = 0;
    }

    // Destroy EGL Images and GBM allocated buffers
    for (int i = 0; i < 4; i++) {
      if (egl_img_in_[i] != EGL_NO_IMAGE_KHR && eglDestroyImageKHR_) {
        eglDestroyImageKHR_(egl_dpy_, egl_img_in_[i]);
        egl_img_in_[i] = EGL_NO_IMAGE_KHR;
      }
      if (tex_in_[i]) {
        glDeleteTextures(1, &tex_in_[i]);
        tex_in_[i] = 0;
      }
      if (gbm_bo_in_[i] && gbm_bo_destroy_) {
        gbm_bo_destroy_(gbm_bo_in_[i]);
        gbm_bo_in_[i] = nullptr;
      }
    }

    if (gbm_dev_ && gbm_device_destroy_) {
      gbm_device_destroy_(gbm_dev_);
      gbm_dev_ = nullptr;
    }

    if (gbm_lib_handle_) {
      dlclose(gbm_lib_handle_);
      gbm_lib_handle_ = nullptr;
    }

    if (egl_dpy_ != EGL_NO_DISPLAY) {
      eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (egl_ctx_ != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy_, egl_ctx_);
      eglTerminate(egl_dpy_);
      egl_dpy_ = EGL_NO_DISPLAY;
    }
  }
};

class Imx95GlesProcessor : public ImxGlesProcessorBase {
public:
  Imx95GlesProcessor() : ImxGlesProcessorBase(SocType::IMX95) {}
};

class Imx8mpGlesProcessor : public ImxGlesProcessorBase {
public:
  Imx8mpGlesProcessor() : ImxGlesProcessorBase(SocType::IMX8MP) {}
};

std::unique_ptr<IVideoProcessor> HalFactory::createVideoProcessor(SocType soc_type) {
  // Check FORCE_MESA_FALLBACK first
  const char *force_mesa = std::getenv("FORCE_MESA_FALLBACK");
  if (force_mesa != nullptr && (strcmp(force_mesa, "1") == 0 || strcmp(force_mesa, "true") == 0)) {
    printf("[HAL] FORCE_MESA_FALLBACK detected. Forcing Mesa software renderer fallback.\n");
    return std::make_unique<HostGlesProcessor>();
  }

  if (soc_type == SocType::IMX8MP) {
    return std::make_unique<Imx8mpGlesProcessor>();
  } else {
    return std::make_unique<Imx95GlesProcessor>();
  }
}
