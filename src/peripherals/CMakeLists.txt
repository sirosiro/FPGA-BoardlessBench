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
    i2c/i2c_eeprom.cpp 
    common/i2c_slave.cpp
)
target_link_libraries(fbb_i2c_eeprom PRIVATE Threads::Threads)

# 2. 仮想UARTループバックエミュレータ
add_executable(fbb_uart_loopback 
    uart/uart_loopback.cpp 
    common/uart_device.cpp
)
target_link_libraries(fbb_uart_loopback PRIVATE Threads::Threads)

# 3. 仮想SPI Flashエミュレータ (W25Q128)
add_executable(fbb_spi_flash
    spi/spi_flash.cpp
    common/spi_slave.cpp
)
target_link_libraries(fbb_spi_flash PRIVATE Threads::Threads)

# 4. 仮想SPI ADCエミュレータ (MCP3208)
add_executable(fbb_spi_adc
    spi/spi_adc.cpp
    common/spi_slave.cpp
)
target_link_libraries(fbb_spi_adc PRIVATE Threads::Threads rt) # 共有メモリ用にrtをリンク
