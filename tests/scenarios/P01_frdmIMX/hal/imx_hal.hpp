#ifndef IMX_HAL_HPP
#define IMX_HAL_HPP

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

enum class SocType { IMX8MP, IMX95 };

class ISerialPort {
public:
  virtual ~ISerialPort() = default;
  virtual bool open(const std::string &device_path) = 0;
  virtual void close() = 0;
  virtual int read(uint8_t *buf, size_t len) = 0;
  virtual int write(const uint8_t *buf, size_t len) = 0;
};

class IGpioController {
public:
  // GPIOピンの入出力方向を定義
  enum class Direction { INPUT, OUTPUT };

  // ピンの状態を定義
  enum class PinState : bool { Low = false, High = true };

  // 検証機能の有無を定義
  enum class Verification { Disable = false, Enable = true };

  virtual ~IGpioController() = default;
  virtual bool readPin(int pin_num) = 0;
  virtual void writePin(int pin_num, PinState state,
                        Verification verify = Verification::Disable) = 0;
  virtual void writePinIL(int pin_num, PinState state,
                          Verification verify = Verification::Disable) = 0;
  virtual void registerInterrupt(int pin_num,
                                 std::function<void(int, bool)> callback,
                                 bool use_filter = false) = 0;

  // シビアなパフォーマンス要求向けに、mmapされた生のGPIOレジスタ仮想アドレスポインタを提供する
  virtual volatile uint32_t *getRawRegisterAddress() = 0;

  // 相互排他インターロックペアの登録API
  virtual void registerInterlockPair(int pin_a, int pin_b) = 0;
};

struct CalibrationData {
  float distortion_k1;
  float distortion_k2;
  float affine_matrix[16];
  float color_balance[4]; // R, G, B, Gamma offset/scale
};

struct VideoFrame {
  const uint8_t* cpu_data; // Pointer to system memory (Simulation fallback or CPU path)
  int dma_buf_fd;          // DMA-BUF File Descriptor (Real board zero-copy path, -1 if invalid)
  int width;
  int height;
  int stride;              // Row stride in bytes
};

class IVideoProcessor {
public:
  virtual ~IVideoProcessor() = default;
  virtual bool initialize() = 0;
  virtual void setCalibrationParams(int camera_index, const CalibrationData& params) = 0;
  virtual bool processFrame(const VideoFrame inputs[4], VideoFrame& output) = 0;
  virtual void terminate() = 0;
};

class IDisplaySink {
public:
  virtual ~IDisplaySink() = default;
  virtual bool initialize() = 0;
  virtual bool outputFrame(const VideoFrame& frame) = 0;
  virtual void terminate() = 0;
};

class HalFactory {
public:
  static SocType detectSocType();
  static std::string getDefaultUartPath(SocType soc_type);
  static std::unique_ptr<ISerialPort>
  createSerialPort(SocType soc_type, const std::string &path = "");
  static std::unique_ptr<IGpioController> createGpioController(
      SocType soc_type,
      const std::unordered_map<int, IGpioController::Direction> &pin_config);
  static std::unique_ptr<IVideoProcessor> createVideoProcessor(SocType soc_type);
  static std::unique_ptr<IDisplaySink> createDisplaySink(SocType soc_type);
};

#endif // IMX_HAL_HPP
