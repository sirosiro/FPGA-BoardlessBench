#include "hal/imx_hal.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional> // std::bind と std::placeholders のため
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

class MainApplication {
private:
  std::unique_ptr<ISerialPort> serial_;
  std::unique_ptr<IGpioController> gpio_;
  std::unique_ptr<IDisplaySink> display_;
  SocType soc_type_;
  std::atomic<bool> running_{true};
  std::thread hdmi_thread_;

  // 共通のGPIO割り込みイベントハンドラ (プライベートメンバ関数)
  void handleGpioEvent(int pin_num, bool state) {
    char notify_msg[128];
    snprintf(notify_msg, sizeof(notify_msg),
             "\r\n[Notification] GPIO Pin %d changed to %s\r\n", pin_num,
             state ? "HIGH" : "LOW");
    printf("[App] GPIO Interrupt triggered on Pin %d! Sending notification via "
           "UART...\n",
           pin_num);
    serial_->write(reinterpret_cast<const uint8_t *>(notify_msg),
                   strlen(notify_msg));
  }

public:
  MainApplication() : soc_type_(SocType::IMX95) {}
  ~MainApplication() {
    stop();
    if (display_) {
      display_->terminate();
    }
  }

  bool isRunning() const { return running_; }
  void stop() {
    running_ = false;
    if (hdmi_thread_.joinable()) {
      hdmi_thread_.join();
    }
  }

  void initialize(SocType soc_type, const std::string &uart_path) {
    soc_type_ = soc_type;
    printf("[App] Initializing HAL for %s...\n",
           (soc_type == SocType::IMX95) ? "i.MX95" : "i.MX 8M Plus");

    // Create Display Sink
    display_ = HalFactory::createDisplaySink(soc_type);
    if (!display_ || !display_->initialize()) {
      fprintf(stderr, "[App] Error: Failed to initialize DisplaySink.\n");
    }

    // ピンアサイン仕様を一箇所に集約して定義 (Approach A)
    // 下位8ピン(0-7)を出力、上位8ピン(8-15)を入力に設定
    using DIR = IGpioController::Direction;
    std::unordered_map<int, DIR> pin_config = {
        // --- 出力ピン (LEDインジケータ等) ---
        {0, DIR::OUTPUT},
        {1, DIR::OUTPUT},
        {2, DIR::OUTPUT},
        {3, DIR::OUTPUT},
        {4, DIR::OUTPUT},
        {5, DIR::OUTPUT},
        {6, DIR::OUTPUT},
        {7, DIR::OUTPUT},

        // --- 入力ピン (スイッチ・センサ入力、割り込み用) ---
        {8, DIR::INPUT},  // 共有割り込みハンドラテスト用
        {9, DIR::INPUT},  // 個別割り込みハンドラテスト用
        {10, DIR::INPUT}, // 共有割り込みハンドラテスト用
        {11, DIR::INPUT}, // 予備入力
        {12, DIR::INPUT}, // 予備入力
        {13, DIR::INPUT}, // 予備入力
        {14, DIR::INPUT}, // 予備入力
        {15, DIR::INPUT}  // 予備入力
    };

    // Create GPIO controller using Factory with pin configuration
    gpio_ = HalFactory::createGpioController(soc_type, pin_config);
    if (!gpio_) {
      fprintf(stderr, "[App] Error: Failed to create GPIO controller.\n");
    }

    // Create Serial Port using Factory
    serial_ = HalFactory::createSerialPort(soc_type, uart_path);
    if (!serial_) {
      fprintf(stderr, "[App] Error: Failed to open serial port: %s\n",
              uart_path.c_str());
    }
  }

  void run() {
    if (!gpio_ || !serial_) {
      fprintf(stderr, "[App] Initialization incomplete. Aborting run.\n");
      stop();
      return;
    }

    using PSTS = IGpioController::PinState;
    using VERIF = IGpioController::Verification;

    // 1. Test GPIO MMIO Access via HAL (Output test on Pin 0-7)
    printf("\n--- 1. Testing GPIO MMIO via HAL ---\n");

    // Read initial state
    bool pin0_init = gpio_->readPin(0);
    printf("[App] Initial state of Pin 0: %d\n", pin0_init);

    // Write test values to output pins (0-7)
    printf("[App] Writing value '1' to Pin 0, 1, 3, 4, 6\n");
    gpio_->writePin(0, PSTS::High);
    gpio_->writePin(1, PSTS::High);
    gpio_->writePin(2, PSTS::Low);
    gpio_->writePin(3, PSTS::High);
    gpio_->writePin(4, PSTS::High);
    gpio_->writePin(5, PSTS::Low);
    gpio_->writePin(6, PSTS::High);
    gpio_->writePin(7, PSTS::Low);

    // Read back values to verify
    bool pin0_new = gpio_->readPin(0);
    bool pin1_new = gpio_->readPin(1);
    bool pin2_new = gpio_->readPin(2);
    bool pin3_new = gpio_->readPin(3);
    bool pin4_new = gpio_->readPin(4);
    bool pin5_new = gpio_->readPin(5);
    bool pin6_new = gpio_->readPin(6);
    bool pin7_new = gpio_->readPin(7);
    printf("[App] Verified states: Pin0=%d, Pin1=%d, Pin2=%d, Pin3=%d, "
           "Pin4=%d, Pin5=%d, Pin6=%d, Pin7=%d\n",
           pin0_new, pin1_new, pin2_new, pin3_new, pin4_new, pin5_new, pin6_new,
           pin7_new);

    if (pin0_new == true && pin1_new == true && pin2_new == false &&
        pin3_new == true && pin4_new == true && pin5_new == false &&
        pin6_new == true && pin7_new == false) {
      printf("[App] GPIO HAL Verification: SUCCESS!\n");
    } else {
      printf("[App] GPIO HAL Verification: FAILURE!\n");
    }

    // 2. Test UART Access via HAL
    printf("\n--- 2. Testing UART via HAL ---\n");
    const char *test_msg = "Hello from i.MX C++ HAL Simulator!\n";
    printf("[App] Writing message: %s", test_msg);

    int written = serial_->write(reinterpret_cast<const uint8_t *>(test_msg),
                                 strlen(test_msg));
    if (written < 0) {
      perror("[App] UART write failed");
    } else {
      printf("[App] Successfully wrote %d bytes via HAL.\n", written);
    }

    // 3. Test GPIO Interrupt via HAL to UART Notification (Direct Lambda
    // callback)
    printf(
        "\n--- 3. Testing GPIO Interrupt via HAL to UART Notification ---\n");
    printf("[App] Registering GPIO Interrupt for Pin 9 (Direct Lambda with "
           "Filter)...\n");

    // ラムダ式を直接コールバックとして登録し、デジタルノイズフィルタを有効化
    // (第3引数 = true)
    gpio_->registerInterrupt(
        9,
        [this](int pin, bool state) {
          char notify_msg[64];
          snprintf(notify_msg, sizeof(notify_msg),
                   "\r\n[Notification] GPIO Pin %d changed directly to %s\r\n",
                   pin, state ? "HIGH" : "LOW");
          printf("[App] GPIO Interrupt triggered on Pin %d (Filtered Direct)! "
                 "Sending notification via UART...\n",
                 pin);
          serial_->write(reinterpret_cast<const uint8_t *>(notify_msg),
                         strlen(notify_msg));
        },
        true);

    // 3.1. Test Shared Callback Interrupts (std::bind to member function)
    printf("\n--- 3.1. Testing Shared Callback Interrupts (std::bind) ---\n");
    printf("[App] Registering GPIO Interrupts for Pin 8 and Pin 10 (Shared "
           "Callback, No Filter)...\n");

    // std::bindを使用してプライベートメンバ関数をコールバックとして登録
    // (デフォルト値 use_filter = false)
    gpio_->registerInterrupt(8, std::bind(&MainApplication::handleGpioEvent,
                                          this, std::placeholders::_1,
                                          std::placeholders::_2));
    gpio_->registerInterrupt(10, std::bind(&MainApplication::handleGpioEvent,
                                           this, std::placeholders::_1,
                                           std::placeholders::_2));

    // 4. Test Advanced Safety Features (WDT / Glitch Filter / Interlock /
    // Verification)
    printf("\n--- 4. Testing Advanced Safety Features ---\n");

    // 4.1. Write Verification test
    printf("[App] Testing writePin with verification (verify = true)...\n");
    gpio_->writePin(0, PSTS::Low, VERIF::Enable);
    printf("[App] Verification test passed. Pin 0 state is %d\n",
           gpio_->readPin(0));

    // 4.2. getRawRegisterAddress test (生アドレス直接アクセス)
    printf("[App] Testing getRawRegisterAddress (direct MMIO read/write)...\n");
    volatile uint32_t *raw_regs = gpio_->getRawRegisterAddress();
    if (raw_regs) {
      printf("[App] Direct writing HIGH to Pin 0 via raw register...\n");
      raw_regs[0] |= (1 << 0); // DRレジスタはオフセット0x00(インデックス0)
      printf("[App] Verified Pin 0 state directly: %d\n",
             (raw_regs[0] & (1 << 0)) ? 1 : 0);
    }

    // 4.3. Mutual Interlock Guard test
    printf("[App] Registering Pin 4 and Pin 5 as mutually exclusive interlock "
           "pair...\n");
    gpio_->registerInterlockPair(4, 5);

    printf("[App] Turning ON Pin 4 via writePinIL...\n");
    gpio_->writePinIL(4, PSTS::High);

    printf("[App] Turning OFF Pin 4 via writePinIL...\n");
    gpio_->writePinIL(4, PSTS::Low);

    // 4.4. 外部シミュレーションピンによるセーフティ＆正常終了デモのトリガー登録
    printf(
        "[App] Registering interactive demo triggers on Pin 11, 12, 13...\n");

    // Pin 11:
    // デモ1（インターロックピンへの通常書き込みによる誤操作ガード・アボート）
    gpio_->registerInterrupt(11, [this](int pin, bool state) {
      if (state) {
        printf("\n[App] [DEMO 1 Triggered on Pin %d] Attempting to write to "
               "interlocked Pin 4 using normal writePin (expecting immediate "
               "abort)...\n",
               pin);
        gpio_->writePin(4, IGpioController::PinState::High);
      }
    });

    // Pin 12: デモ2（排他ピンの同時ONによる競合防止ガード・アボート）
    gpio_->registerInterrupt(12, [this](int pin, bool state) {
      if (state) {
        printf(
            "\n[App] [DEMO 2 Triggered on Pin %d] Attempting to turn ON both "
            "Pin 4 and Pin 5 via writePinIL (expecting immediate abort)...\n",
            pin);
        gpio_->writePinIL(4, IGpioController::PinState::High);
        gpio_->writePinIL(5, IGpioController::PinState::High);
      }
    });

    // Pin 13: 正常終了デモ
    gpio_->registerInterrupt(13, [this](int pin, bool state) {
      if (state) {
        printf("\n[App] [DEMO 3 Triggered on Pin %d] Requesting clean "
               "application shutdown...\n",
               pin);
        stop();
      }
    });
    // 5. Test OpenGL ES via HAL (Distortion Correction Simulation)
    printf("\n--- 5. Testing OpenGL ES (Image Processing) via HAL ---\n");
    auto processor = HalFactory::createVideoProcessor(soc_type_);
    if (processor && processor->initialize()) {
      uint8_t camera_frames[4][64 * 64 * 4];
      const uint8_t *in_frames[4];
      uint8_t out_img[64 * 64 * 4];

      for (int i = 0; i < 4; i++) {
        in_frames[i] = camera_frames[i];
        memset(camera_frames[i], 0, sizeof(camera_frames[i]));
      }

      // Camera 0: Red (R=255, G=0, B=0, A=255)
      // Camera 1: Green (R=0, G=255, B=0, A=255)
      // Camera 2: Blue (R=0, G=0, B=255, A=255)
      // Camera 3: Yellow (R=255, G=255, B=0, A=255)
      for (int p = 0; p < 64 * 64; p++) {
        camera_frames[0][p * 4 + 0] = 255;
        camera_frames[0][p * 4 + 3] = 255;

        camera_frames[1][p * 4 + 1] = 255;
        camera_frames[1][p * 4 + 3] = 255;

        camera_frames[2][p * 4 + 2] = 255;
        camera_frames[2][p * 4 + 3] = 255;

        camera_frames[3][p * 4 + 0] = 255;
        camera_frames[3][p * 4 + 1] = 255;
        camera_frames[3][p * 4 + 3] = 255;
      }

      // Configure Calibration parameters for 4 cameras individually
      for (int i = 0; i < 4; i++) {
        CalibrationData params;
        params.distortion_k1 = 0.0f;
        params.distortion_k2 = 0.0f;
        memset(params.affine_matrix, 0, sizeof(params.affine_matrix));
        params.affine_matrix[0] = 1.0f;
        params.affine_matrix[5] = 1.0f;
        params.affine_matrix[10] = 1.0f;
        params.affine_matrix[15] = 1.0f;
        params.color_balance[0] = 1.0f;
        params.color_balance[1] = 1.0f;
        params.color_balance[2] = 1.0f;
        params.color_balance[3] = 1.0f; // Gamma

        processor->setCalibrationParams(i, params);
      }

      printf("[App] Processing 4x 64x64 input frames via GPU processor "
             "(Stitching)...\n");
      if (processor->processFrame(in_frames, out_img, 64, 64)) {
        bool success = true;
        for (int y = 0; y < 64; y++) {
          for (int x = 0; x < 64; x++) {
            int pixel_idx = (y * 64 + x) * 4;
            uint8_t r = out_img[pixel_idx + 0];
            uint8_t g = out_img[pixel_idx + 1];
            uint8_t b = out_img[pixel_idx + 2];
            uint8_t a = out_img[pixel_idx + 3];

            uint8_t exp_r = 0, exp_g = 0, exp_b = 0;
            if (y < 32) {
              if (x < 32) { // Bottom-Left (Cam 0: Red -> Inverted: Cyan)
                exp_r = 0;
                exp_g = 255;
                exp_b = 255;
              } else { // Bottom-Right (Cam 1: Green -> Inverted: Magenta)
                exp_r = 255;
                exp_g = 0;
                exp_b = 255;
              }
            } else {
              if (x < 32) { // Top-Left (Cam 2: Blue -> Inverted: Yellow)
                exp_r = 255;
                exp_g = 255;
                exp_b = 0;
              } else { // Top-Right (Cam 3: Yellow -> Inverted: Blue)
                exp_r = 0;
                exp_g = 0;
                exp_b = 255;
              }
            }

            if (r != exp_r || g != exp_g || b != exp_b || a != 255) {
              success = false;
              break;
            }
          }
          if (!success)
            break;
        }

        if (success) {
          printf("[App] OpenGL ES 4-Cam Stitching verification: SUCCESS! "
                 "(Stitched & inverted correctly)\n");
        } else {
          printf("[App] OpenGL ES 4-Cam Stitching verification: FAILURE! "
                 "(Pixel values mismatch)\n");
        }

        // Output first verification frame to display
        if (display_) {
          display_->outputFrame(out_img, 64, 64);
        }
      } else {
        printf("[App] OpenGL ES processing failed.\n");
      }
      processor->terminate();
    } else {
      printf("[App] Error: Failed to initialize VideoProcessor.\n");
    }

    // VFPGA_INTERACTIVE
    // 環境変数を確認し、非インタラクティブ時は自動で終了させる
    const char *interactive_env = std::getenv("VFPGA_INTERACTIVE");
    bool is_interactive =
        (interactive_env != nullptr && interactive_env[0] == '1');
    if (!is_interactive) {
      printf("[App] Non-interactive automated test mode. Shutting down "
             "automatically...\n");
      stop();
    } else {
      printf("[App] Interactive Mode. Starting HDMI display preview thread (10 fps)...\n");
      hdmi_thread_ = std::thread([this]() {
        auto processor = HalFactory::createVideoProcessor(soc_type_);
        if (!processor || !processor->initialize()) {
          fprintf(stderr, "[App] [HDMI Thread] Failed to initialize VideoProcessor.\n");
          return;
        }

        uint8_t camera_frames[4][64 * 64 * 4];
        const uint8_t *in_frames[4];
        uint8_t out_img[64 * 64 * 4];
        for (int i = 0; i < 4; i++) {
          in_frames[i] = camera_frames[i];
          memset(camera_frames[i], 0, sizeof(camera_frames[i]));
        }

        for (int p = 0; p < 64 * 64; p++) {
          camera_frames[0][p * 4 + 0] = 255; camera_frames[0][p * 4 + 3] = 255; // Red
          camera_frames[1][p * 4 + 1] = 255; camera_frames[1][p * 4 + 3] = 255; // Green
          camera_frames[2][p * 4 + 2] = 255; camera_frames[2][p * 4 + 3] = 255; // Blue
          camera_frames[3][p * 4 + 0] = 255; camera_frames[3][p * 4 + 1] = 255; camera_frames[3][p * 4 + 3] = 255; // Yellow
        }

        float theta = 0.0f;
        while (running_) {
          theta += 0.05f;
          float scale = 0.8f + 0.2f * sin(theta);
          float cos_t = cos(theta * 0.2f);
          float sin_t = sin(theta * 0.2f);

          for (int i = 0; i < 4; i++) {
            CalibrationData params;
            params.distortion_k1 = -0.1f * (float)(i + 1) * (0.5f + 0.5f * sin(theta));
            params.distortion_k2 = 0.0f;

            memset(params.affine_matrix, 0, sizeof(params.affine_matrix));
            params.affine_matrix[0] = scale * cos_t;
            params.affine_matrix[1] = -sin_t;
            params.affine_matrix[4] = sin_t;
            params.affine_matrix[5] = scale * cos_t;
            params.affine_matrix[10] = 1.0f;
            params.affine_matrix[15] = 1.0f;

            params.color_balance[0] = 0.8f + 0.2f * sin(theta + (float)i);
            params.color_balance[1] = 0.8f + 0.2f * cos(theta + (float)i);
            params.color_balance[2] = 0.8f + 0.2f * sin(theta * 0.5f);
            params.color_balance[3] = 1.0f;

            processor->setCalibrationParams(i, params);
          }

          processor->processFrame(in_frames, out_img, 64, 64);
          if (display_) {
            display_->outputFrame(out_img, 64, 64);
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        processor->terminate();
      });

      printf("[App] Setup complete. Waiting for Pin 8, 9, 10 (UART notify) or "
             "Pin 11, 12, 13 (Demo triggers)...\n");
    }
  }
};

int main() {
  printf("--- P01_frdmIMX C++ HAL Test Start ---\n");

  // Automatically detect SOC Type and default UART path via HAL Factory
  SocType soc_type = HalFactory::detectSocType();
  std::string uart_path = HalFactory::getDefaultUartPath(soc_type);

  MainApplication app;
  app.initialize(soc_type, uart_path);
  app.run();

  while (app.isRunning()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  printf("\n--- P01_frdmIMX C++ HAL Test End ---\n");
  return 0;
}
