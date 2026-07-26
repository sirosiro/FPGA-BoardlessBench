# F-BB 共有ペリフェラル・ライブラリのビルド定義

if(CMAKE_CROSSCOMPILING)
    return()
endif()

# C++17規格の指定
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 成果物はビルドディレクトリの bin/ に出力する
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# 1. 仮想I2C EEPROMエミュレータ
add_executable(fbb_i2c_eeprom 
    official_plugins/microchip_at24c02c/i2c_eeprom.cpp 
    common/i2c_slave.cpp
)
target_link_libraries(fbb_i2c_eeprom PRIVATE Threads::Threads)

# 2. 仮想UARTループバックエミュレータ
add_executable(fbb_uart_loopback 
    official_plugins/generic_uart_loopback/uart_loopback.cpp 
    common/uart_device.cpp
)
target_link_libraries(fbb_uart_loopback PRIVATE Threads::Threads)

# 3. 仮想SPI Flashエミュレータ (W25Q128)
add_executable(fbb_spi_flash
    official_plugins/winbond_w25q128/spi_flash.cpp
    common/spi_slave.cpp
)
target_link_libraries(fbb_spi_flash PRIVATE Threads::Threads)

# 4. 仮想SPI ADCエミュレータ (MCP3208)
add_executable(fbb_spi_adc
    official_plugins/microchip_mcp3208/spi_adc.cpp
    common/spi_slave.cpp
)
target_link_libraries(fbb_spi_adc PRIVATE Threads::Threads rt) # 共有メモリ用にrtをリンク

# 5. 仮想I2C OLEDディスプレイエミュレータ (SSD1306)
add_executable(fbb_i2c_oled
    official_plugins/solomon_ssd1306/ssd1306.cpp
    common/i2c_slave.cpp
)
target_link_libraries(fbb_i2c_oled PRIVATE Threads::Threads rt) # 共有メモリ用にrtをリンク
