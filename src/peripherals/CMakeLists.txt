# F-BB 共有ペリフェラル・ライブラリのビルド定義

if(CMAKE_CROSSCOMPILING)
    return()
endif()

# 成果物はビルドディレクトリの bin/ に出力する
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# 1. 仮想I2C EEPROMエミュレータ
add_executable(fbb_i2c_eeprom i2c/i2c_eeprom.c)
target_link_libraries(fbb_i2c_eeprom PRIVATE Threads::Threads)

# 2. 仮想UARTループバックエミュレータ
add_executable(fbb_uart_loopback uart/uart_loopback.c)
target_link_libraries(fbb_uart_loopback PRIVATE Threads::Threads)
