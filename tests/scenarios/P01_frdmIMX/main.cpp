#include "hal/imx_hal.hpp"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <functional> // std::bind と std::placeholders のため
#include <unordered_map>


class MainApplication {
private:
    std::unique_ptr<ISerialPort> serial_;
    std::unique_ptr<IGpioController> gpio_;
    SocType soc_type_;

    // 共通のGPIO割り込みイベントハンドラ (プライベートメンバ関数)
    void handleGpioEvent(int pin_num, bool state) {
        char notify_msg[128];
        snprintf(notify_msg, sizeof(notify_msg), 
                 "\r\n[Notification] GPIO Pin %d changed to %s\r\n", 
                 pin_num, state ? "HIGH" : "LOW");
        printf("[App] GPIO Interrupt triggered on Pin %d! Sending notification via UART...\n", pin_num);
        serial_->write(reinterpret_cast<const uint8_t*>(notify_msg), strlen(notify_msg));
    }

public:
    MainApplication() : soc_type_(SocType::IMX95) {}

    void initialize(SocType soc_type, const std::string& uart_path) {
        soc_type_ = soc_type;
        printf("[App] Initializing HAL for %s...\n", 
               (soc_type == SocType::IMX95) ? "i.MX95" : "i.MX 8M Plus");

        // ピンアサイン仕様を一箇所に集約して定義 (Approach A)
        // 下位8ピン(0-7)を出力、上位8ピン(8-15)を入力に設定
        std::unordered_map<int, IGpioController::Direction> pin_config = {
            // --- 出力ピン (LEDインジケータ等) ---
            {0, IGpioController::Direction::OUTPUT},
            {1, IGpioController::Direction::OUTPUT},
            {2, IGpioController::Direction::OUTPUT},
            {3, IGpioController::Direction::OUTPUT},
            {4, IGpioController::Direction::OUTPUT},
            {5, IGpioController::Direction::OUTPUT},
            {6, IGpioController::Direction::OUTPUT},
            {7, IGpioController::Direction::OUTPUT},

            // --- 入力ピン (スイッチ・センサ入力、割り込み用) ---
            {8, IGpioController::Direction::INPUT},  // 共有割り込みハンドラテスト用 (std::bind)
            {9, IGpioController::Direction::INPUT},  // 個別割り込みハンドラテスト用 (ラムダ)
            {10, IGpioController::Direction::INPUT}, // 共有割り込みハンドラテスト用 (std::bind)
            {11, IGpioController::Direction::INPUT}, // 予備入力
            {12, IGpioController::Direction::INPUT}, // 予備入力
            {13, IGpioController::Direction::INPUT}, // 予備入力
            {14, IGpioController::Direction::INPUT}, // 予備入力
            {15, IGpioController::Direction::INPUT}  // 予備入力
        };

        // Create GPIO controller using Factory with pin configuration
        gpio_ = HalFactory::createGpioController(soc_type, pin_config);
        if (!gpio_) {
            fprintf(stderr, "[App] Error: Failed to create GPIO controller.\n");
        }

        // Create Serial Port using Factory
        serial_ = HalFactory::createSerialPort(soc_type, uart_path);
        if (!serial_) {
            fprintf(stderr, "[App] Error: Failed to open serial port: %s\n", uart_path.c_str());
        }
    }

    void run() {
        if (!gpio_ || !serial_) {
            fprintf(stderr, "[App] Initialization incomplete. Aborting run.\n");
            return;
        }

        // 1. Test GPIO MMIO Access via HAL (Output test on Pin 0-7)
        printf("\n--- 1. Testing GPIO MMIO via HAL ---\n");
        
        // Read initial state
        bool pin0_init = gpio_->readPin(0);
        printf("[App] Initial state of Pin 0: %d\n", pin0_init);

        // Write test values to output pins (0-7)
        printf("[App] Writing value '1' to Pin 0, 1, 3, 4, 6\n");
        gpio_->writePin(0, true);
        gpio_->writePin(1, true);
        gpio_->writePin(2, false);
        gpio_->writePin(3, true);
        gpio_->writePin(4, true);
        gpio_->writePin(5, false);
        gpio_->writePin(6, true);
        gpio_->writePin(7, false);

        // Read back values to verify
        bool pin0_new = gpio_->readPin(0);
        bool pin1_new = gpio_->readPin(1);
        bool pin2_new = gpio_->readPin(2);
        bool pin3_new = gpio_->readPin(3);
        bool pin4_new = gpio_->readPin(4);
        bool pin5_new = gpio_->readPin(5);
        bool pin6_new = gpio_->readPin(6);
        bool pin7_new = gpio_->readPin(7);
        printf("[App] Verified states: Pin0=%d, Pin1=%d, Pin2=%d, Pin3=%d, Pin4=%d, Pin5=%d, Pin6=%d, Pin7=%d\n", 
               pin0_new, pin1_new, pin2_new, pin3_new, pin4_new, pin5_new, pin6_new, pin7_new);

        if (pin0_new == true && pin1_new == true && pin2_new == false && pin3_new == true &&
            pin4_new == true && pin5_new == false && pin6_new == true && pin7_new == false) {
            printf("[App] GPIO HAL Verification: SUCCESS!\n");
        } else {
            printf("[App] GPIO HAL Verification: FAILURE!\n");
        }

        // 2. Test UART Access via HAL
        printf("\n--- 2. Testing UART via HAL ---\n");
        const char *test_msg = "Hello from i.MX C++ HAL Simulator!\n";
        printf("[App] Writing message: %s", test_msg);
        
        int written = serial_->write(reinterpret_cast<const uint8_t*>(test_msg), strlen(test_msg));
        if (written < 0) {
            perror("[App] UART write failed");
        } else {
            printf("[App] Successfully wrote %d bytes via HAL.\n", written);
        }

        // 3. Test GPIO Interrupt via HAL to UART Notification (Direct Lambda callback)
        printf("\n--- 3. Testing GPIO Interrupt via HAL to UART Notification ---\n");
        printf("[App] Registering GPIO Interrupt for Pin 9 (Direct Lambda)...\n");
        
        // ここはラムダ式で直接シリアルへの書き込みを処理する (引数に pin_num, value が渡される)
        gpio_->registerInterrupt(9, [this](int pin, bool state) {
            char notify_msg[64];
            snprintf(notify_msg, sizeof(notify_msg), "\r\n[Notification] GPIO Pin %d changed directly to %s\r\n", pin, state ? "HIGH" : "LOW");
            printf("[App] GPIO Interrupt triggered on Pin %d (Direct)! Sending notification via UART...\n", pin);
            serial_->write(reinterpret_cast<const uint8_t*>(notify_msg), strlen(notify_msg));
        });

        // 3.1. Test Shared Callback Interrupts (std::bind to member function)
        printf("\n--- 3.1. Testing Shared Callback Interrupts (std::bind) ---\n");
        printf("[App] Registering GPIO Interrupts for Pin 8 and Pin 10 (Shared Callback Demo)...\n");

        // std::bind を使用してメンバ関数を直接バインド！
        gpio_->registerInterrupt(8, std::bind(&MainApplication::handleGpioEvent, this, std::placeholders::_1, std::placeholders::_2));
        gpio_->registerInterrupt(10, std::bind(&MainApplication::handleGpioEvent, this, std::placeholders::_1, std::placeholders::_2));

        printf("[App] Setup complete. Waiting for Pin 8, 9, or 10 changes (Toggle them in Web Dashboard)...\n");
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

    while(1);

    printf("\n--- P01_frdmIMX C++ HAL Test End ---\n");
    return 0;
}
