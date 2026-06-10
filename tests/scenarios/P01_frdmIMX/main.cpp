#include "hal/imx_hal.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional> // std::bind と std::placeholders のため
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// BMPロード関数
bool loadBMP(const std::string& filename, std::vector<uint8_t>& out_rgba, int& out_width, int& out_height) {
  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    std::cerr << "[App] Failed to open " << filename << std::endl;
    return false;
  }

  uint8_t header[54];
  file.read(reinterpret_cast<char*>(header), 54);
  if (file.gcount() != 54 || header[0] != 'B' || header[1] != 'M') {
    std::cerr << "[App] Invalid BMP file: " << filename << std::endl;
    return false;
  }

  int width = *reinterpret_cast<int*>(&header[18]);
  int height = *reinterpret_cast<int*>(&header[22]);
  int bits = *reinterpret_cast<short*>(&header[28]);
  int data_offset = *reinterpret_cast<int*>(&header[10]);

  if (bits != 24 && bits != 32) {
    std::cerr << "[App] Unsupported BMP format (must be 24 or 32 bits): " << filename << std::endl;
    return false;
  }

  file.seekg(data_offset, std::ios::beg);

  int bytes_per_pixel = bits / 8;
  int row_stride = (width * bytes_per_pixel + 3) & ~3; // 4バイトアライメント
  std::vector<uint8_t> row_buf(row_stride);

  out_rgba.resize(width * height * 4);
  out_width = width;
  out_height = height;

  for (int y = 0; y < height; ++y) {
    file.read(reinterpret_cast<char*>(row_buf.data()), row_stride);
    int dest_y = height - 1 - y;
    for (int x = 0; x < width; ++x) {
      int src_idx = x * bytes_per_pixel;
      int dst_idx = (dest_y * width + x) * 4;
      if (bits == 32) {
        out_rgba[dst_idx + 0] = row_buf[src_idx + 2]; // R
        out_rgba[dst_idx + 1] = row_buf[src_idx + 1]; // G
        out_rgba[dst_idx + 2] = row_buf[src_idx + 0]; // B
        out_rgba[dst_idx + 3] = row_buf[src_idx + 3]; // A
      } else {
        out_rgba[dst_idx + 0] = row_buf[src_idx + 2]; // R
        out_rgba[dst_idx + 1] = row_buf[src_idx + 1]; // G
        out_rgba[dst_idx + 2] = row_buf[src_idx + 0]; // B
        out_rgba[dst_idx + 3] = 255;                  // A
      }
    }
  }
  return true;
}

class MainApplication {
private:
  std::unique_ptr<ISerialPort> serial_;
  std::unique_ptr<IGpioController> gpio_;
  std::unique_ptr<IDisplaySink> display_;
  SocType soc_type_;
  std::atomic<bool> running_{true};
  std::atomic<bool> preview_running_{true};
  std::thread hdmi_thread_;

  // 共通のGPIO割り込みイベントハンドラ (プライベートメンバ関数)
  void handleGpioEvent(int pin_num, bool state) {
    char notify_msg[256];
    const char* role = (pin_num == 8 || pin_num == 10) ? "Shared Interrupt Test Pin" : "Unknown Pin";
    snprintf(notify_msg, sizeof(notify_msg),
             "\r\n[Notification] GPIO Pin %d (%s) changed to %s\r\n", pin_num,
             role, state ? "HIGH" : "LOW");
    printf("[App] GPIO Interrupt triggered on Pin %d (%s)! Sending notification via "
           "UART...\n",
           pin_num, role);
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
    } else {
      const char* pin_assign_msg = 
          "\r\n================ GPIO Pin Assignment & Roles ================\r\n"
          "  [Pin 0]  OUTPUT - LED Indicator 0 (Write test)\r\n"
          "  [Pin 1]  OUTPUT - LED Indicator 1 (Write test)\r\n"
          "  [Pin 2]  OUTPUT - LED Indicator 2 (Write test)\r\n"
          "  [Pin 3]  OUTPUT - LED Indicator 3 (Write test)\r\n"
          "  [Pin 4]  OUTPUT - Interlock Output A (Safety pair with Pin 5)\r\n"
          "  [Pin 5]  OUTPUT - Interlock Output B (Safety pair with Pin 4)\r\n"
          "  [Pin 6]  OUTPUT - LED Indicator 6 (Write test)\r\n"
          "  [Pin 7]  OUTPUT - LED Indicator 7 (Write test)\r\n"
          "  [Pin 8]  INPUT  - Shared Interrupt Test Pin (No filter)\r\n"
          "  [Pin 9]  INPUT  - Direct Lambda Interrupt Test Pin (Filtered)\r\n"
          "  [Pin 10] INPUT  - Shared Interrupt Test Pin (No filter)\r\n"
          "  [Pin 11] INPUT  - DEMO 1 Trigger (Normal write to interlocked Pin 4 -> Aborts)\r\n"
          "  [Pin 12] INPUT  - DEMO 2 Trigger (Turn on both Pin 4 & 5 -> Aborts)\r\n"
          "  [Pin 13] INPUT  - DEMO 3 Trigger (Clean application shutdown)\r\n"
          "  [Pin 14] INPUT  - Switch to Automotive Stitching Test (Stops rotation preview)\r\n"
          "  [Pin 15] INPUT  - Spare input\r\n"
          "==============================================================\r\n";
      printf("%s", pin_assign_msg);
      serial_->write(reinterpret_cast<const uint8_t*>(pin_assign_msg), strlen(pin_assign_msg));
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
          char notify_msg[256];
          snprintf(notify_msg, sizeof(notify_msg),
                   "\r\n[Notification] GPIO Pin %d (Direct Lambda Interrupt Test Pin) changed directly to %s\r\n",
                   pin, state ? "HIGH" : "LOW");
          printf("[App] GPIO Interrupt triggered on Pin %d (Direct Lambda / Filtered)! "
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
        printf("\n[App] [DEMO 1 Triggered on Pin %d (DEMO 1: Normal write to Pin 4)] Attempting to write to "
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
            "\n[App] [DEMO 2 Triggered on Pin %d (DEMO 2: Turn on both Pin 4 & 5)] Attempting to turn ON both "
            "Pin 4 and Pin 5 via writePinIL (expecting immediate abort)...\n",
            pin);
        gpio_->writePinIL(4, IGpioController::PinState::High);
        gpio_->writePinIL(5, IGpioController::PinState::High);
      }
    });

    // Pin 13: 正常終了デモ
    gpio_->registerInterrupt(13, [this](int pin, bool state) {
      if (state) {
        printf("\n[App] [DEMO 3 Triggered on Pin %d (DEMO 3: Clean application shutdown)] Requesting clean "
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
      uint8_t out_img[64 * 64 * 4];
      VideoFrame inputs[4];
      VideoFrame output;

      for (int i = 0; i < 4; i++) {
        memset(camera_frames[i], 0, sizeof(camera_frames[i]));
        inputs[i].cpu_data = camera_frames[i];
        inputs[i].dma_buf_fd = -1;
        inputs[i].width = 64;
        inputs[i].height = 64;
        inputs[i].stride = 64 * 4;
      }
      output.cpu_data = out_img;
      output.dma_buf_fd = -1;
      output.width = 64;
      output.height = 64;
      output.stride = 64 * 4;

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
      if (processor->processFrame(inputs, output)) {
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
          display_->outputFrame(output);
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
      preview_running_ = true;

      // Pin 14: 車載合成テストへの移行トリガー登録
      gpio_->registerInterrupt(14, [this](int pin, bool state) {
        if (state) {
          printf("\n[App] [Triggered on Pin %d (Switch to Automotive Stitching Test)] Stopping rotation preview thread...\n", pin);
          preview_running_ = false;
        }
      });

      hdmi_thread_ = std::thread([this]() {
        auto processor = HalFactory::createVideoProcessor(soc_type_);
        if (!processor || !processor->initialize()) {
          fprintf(stderr, "[App] [HDMI Thread] Failed to initialize VideoProcessor.\n");
          return;
        }

        std::vector<std::vector<uint8_t>> loaded_frames(4);
        bool use_patterns = true;
        for (int i = 0; i < 4; i++) {
          std::string filename = "tests/scenarios/P01_frdmIMX/images/pattern" + std::to_string(i) + ".bmp";
          int w = 0, h = 0;
          if (!loadBMP(filename, loaded_frames[i], w, h) || w != 64 || h != 64) {
            use_patterns = false;
            break;
          }
        }

        uint8_t camera_frames[4][64 * 64 * 4];
        uint8_t out_img[64 * 64 * 4];
        VideoFrame inputs[4];
        VideoFrame output;
        for (int i = 0; i < 4; i++) {
          inputs[i].cpu_data = camera_frames[i];
          inputs[i].dma_buf_fd = -1;
          inputs[i].width = 64;
          inputs[i].height = 64;
          inputs[i].stride = 64 * 4;
          if (use_patterns) {
            memcpy(camera_frames[i], loaded_frames[i].data(), 64 * 64 * 4);
          } else {
            memset(camera_frames[i], 0, sizeof(camera_frames[i]));
          }
        }
        output.cpu_data = out_img;
        output.dma_buf_fd = -1;
        output.width = 64;
        output.height = 64;
        output.stride = 64 * 4;

        if (!use_patterns) {
          printf("[App] Warning: Failed to load 64x64 patterns. Falling back to solid colors.\n");
          for (int p = 0; p < 64 * 64; p++) {
            camera_frames[0][p * 4 + 0] = 255; camera_frames[0][p * 4 + 3] = 255; // Red
            camera_frames[1][p * 4 + 1] = 255; camera_frames[1][p * 4 + 3] = 255; // Green
            camera_frames[2][p * 4 + 2] = 255; camera_frames[2][p * 4 + 3] = 255; // Blue
            camera_frames[3][p * 4 + 0] = 255; camera_frames[3][p * 4 + 1] = 255; camera_frames[3][p * 4 + 3] = 255; // Yellow
          }
        } else {
          printf("[App] Successfully loaded 64x64 patterns for preview.\n");
        }

        float theta = 0.0f;
        while (running_ && preview_running_) {
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

          processor->processFrame(inputs, output);
          if (display_) {
            display_->outputFrame(output);
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        processor->terminate();
      });

      printf("[App] Setup complete. Waiting for Pin 8, 9, 10 (UART notify), Pin 11, 12, 13 (Demo triggers), or Pin 14 (Automotive Switch)...\n");

      if (hdmi_thread_.joinable()) {
        hdmi_thread_.join();
      }

      if (running_) {
        runAutomotiveStitchingTest();
      }
    }
  }

  static void resizeRGBA(const uint8_t* src, int src_w, int src_h, uint8_t* dst, int dst_w, int dst_h) {
    for (int y = 0; y < dst_h; y++) {
      int src_y = (y * src_h) / dst_h;
      for (int x = 0; x < dst_w; x++) {
        int src_x = (x * src_w) / dst_w;
        int src_idx = (src_y * src_w + src_x) * 4;
        int dst_idx = (y * dst_w + x) * 4;
        dst[dst_idx + 0] = src[src_idx + 0];
        dst[dst_idx + 1] = src[src_idx + 1];
        dst[dst_idx + 2] = src[src_idx + 2];
        dst[dst_idx + 3] = src[src_idx + 3];
      }
    }
  }

  void runAutomotiveStitchingTest() {
    printf("\n--- 6. Running Automotive Stitching Test (BMP Loader from images/) ---\n");

    std::vector<std::vector<uint8_t>> camera_frames(4);
    int cam_w = 0, cam_h = 0;

    for (int i = 0; i < 4; ++i) {
      std::string filename = "tests/scenarios/P01_frdmIMX/images/cam" + std::to_string(i) + ".bmp";
      printf("[App] Loading Simulation Camera Frame: %s\n", filename.c_str());
      int w = 0, h = 0;
      if (!loadBMP(filename, camera_frames[i], w, h)) {
        fprintf(stderr, "[App] Error: Failed to load %s\n", filename.c_str());
        return;
      }
      if (i == 0) {
        cam_w = w;
        cam_h = h;
      } else if (w != cam_w || h != cam_h) {
        fprintf(stderr, "[App] Error: Image dimensions mismatch between cameras.\n");
        return;
      }
    }
    printf("[App] Loaded 4 camera frames. Frame size: %dx%d\n", cam_w, cam_h);

    auto processor = HalFactory::createVideoProcessor(soc_type_);
    if (processor && processor->initialize()) {
      // 歪み補正係数 k1 (4台のカメラは同じレンズを使用しているため共通にするべきです)
      // 過補正を防ぐため、小さめの負の値（例: -0.05f 〜 -0.08f）に設定します。
      // もし補正の方向自体が逆である場合は、正の値（例: +0.05f）を試してください。
      float k1_val = -0.12f; 
      for (int i = 0; i < 4; i++) {
        CalibrationData params;
        params.distortion_k1 = k1_val;
        params.distortion_k2 = 0.0f;
        memset(params.affine_matrix, 0, sizeof(params.affine_matrix));
        params.affine_matrix[10] = 1.0f; // z-scale
        
        // 各カメラごとのアラウンドビュー用透視投影行列 (ホモグラフィ)
        // 車体サイズ分の空間 (X: ±0.2, Y: ±0.4) を確保し、Y反転テクスチャに対応したシフトでボディを完全に隠す
        // 新しいAroundViewシェーダーではシェーダー内部でUV座標を直接計算するため、
        // アフィン行列はアイデンティティ（微調整用）に設定する。
        params.affine_matrix[0] = 1.0f;   // X scale
        params.affine_matrix[5] = 1.0f;   // Y scale
        params.affine_matrix[15] = 1.0f;  // W

        params.color_balance[0] = 1.0f;
        params.color_balance[1] = 1.0f;
        params.color_balance[2] = 1.0f;
        params.color_balance[3] = -1.0f; // ガンマが負なら色反転をオフにする
        processor->setCalibrationParams(i, params);
      }

      int total_w = 1920;
      int total_h = 1080;
      int av_w = 1080;
      int av_h = 1080;
      std::vector<uint8_t> av_img(av_w * av_h * 4);

      VideoFrame inputs[4];
      VideoFrame output;
      for (int i = 0; i < 4; i++) {
        inputs[i].cpu_data = camera_frames[i].data();
        inputs[i].dma_buf_fd = -1;
        inputs[i].width = cam_w;
        inputs[i].height = cam_h;
        inputs[i].stride = cam_w * 4;
      }
      output.cpu_data = av_img.data();
      output.dma_buf_fd = -1;
      output.width = av_w;
      output.height = av_h;
      output.stride = av_w * 4;

      printf("[App] Stitching 4 camera frames into AroundView image %dx%d...\n", av_w, av_h);
      if (processor->processFrame(inputs, output)) {
        printf("[App] Stitching successful. Outputting side-by-side composite to display...\n");
        // OpenGLのボトムアップ出力をトップダウンに上下反転する
        std::vector<uint8_t> av_top_down(av_w * av_h * 4);
        for (int y = 0; y < av_h; y++) {
          const uint8_t* src_row = av_img.data() + (av_h - 1 - y) * av_w * 4;
          uint8_t* dst_row = av_top_down.data() + y * av_w * 4;
          memcpy(dst_row, src_row, av_w * 4);
        }

        std::vector<uint8_t> out_img(total_w * total_h * 4, 0); // initialize to black/0

        // Layout parameters
        int gap = 20;
        int left_w = total_w - av_w; // 840
        int cell_w = (left_w - gap * 3) / 2; // 390
        int cell_h = 312; // cell_w / 1.25 = 390 / 1.25 = 312
        int grid_h = cell_h * 2 + gap * 3; // 684
        int y_offset = (total_h - grid_h) / 2; // 198

        // Resize the 4 raw camera frames
        std::vector<std::vector<uint8_t>> resized_frames(4, std::vector<uint8_t>(cell_w * cell_h * 4));
        for (int i = 0; i < 4; i++) {
          resizeRGBA(camera_frames[i].data(), cam_w, cam_h, resized_frames[i].data(), cell_w, cell_h);
        }

        // 1. Copy Left 2x2 grid with gaps
        // cam0 (Front): top-left -> x = gap, y = y_offset + gap
        // cam1 (Rear): top-right -> x = gap + cell_w + gap, y = y_offset + gap
        // cam2 (Left): bottom-left -> x = gap, y = y_offset + gap + cell_h + gap
        // cam3 (Right): bottom-right -> x = gap + cell_w + gap, y = y_offset + gap + cell_h + gap
        for (int y = 0; y < cell_h; y++) {
          // cam0: row y -> final row (y_offset + gap + y)
          memcpy(out_img.data() + ((y_offset + gap + y) * total_w + gap) * 4,
                 resized_frames[0].data() + y * cell_w * 4,
                 cell_w * 4);
          // cam1: row y -> final row (y_offset + gap + y)
          memcpy(out_img.data() + ((y_offset + gap + y) * total_w + gap + cell_w + gap) * 4,
                 resized_frames[1].data() + y * cell_w * 4,
                 cell_w * 4);
          // cam2: row y -> final row (y_offset + gap + cell_h + gap + y)
          memcpy(out_img.data() + ((y_offset + gap + cell_h + gap + y) * total_w + gap) * 4,
                 resized_frames[2].data() + y * cell_w * 4,
                 cell_w * 4);
          // cam3: row y -> final row (y_offset + gap + cell_h + gap + y)
          memcpy(out_img.data() + ((y_offset + gap + cell_h + gap + y) * total_w + gap + cell_w + gap) * 4,
                 resized_frames[3].data() + y * cell_w * 4,
                 cell_w * 4);
        }

        // 2. Copy Right half (AroundView top-down)
        // AroundView starts at x offset = left_w, y offset = 0
        for (int y = 0; y < av_h; y++) {
          memcpy(out_img.data() + (y * total_w + left_w) * 4,
                 av_top_down.data() + y * av_w * 4,
                 av_w * 4);
        }

        if (display_) {
          VideoFrame display_frame;
          display_frame.cpu_data = out_img.data();
          display_frame.dma_buf_fd = -1;
          display_frame.width = total_w;
          display_frame.height = total_h;
          display_frame.stride = total_w * 4;
          display_->outputFrame(display_frame);
        }
      } else {
        fprintf(stderr, "[App] Error: Failed to composite frames.\n");
      }
      processor->terminate();
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
