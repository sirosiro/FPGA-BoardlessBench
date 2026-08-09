# F-BB 共有ペリフェラル・ライブラリのビルド定義 (PPA 2.0 開発者向け自動発見対応)

if(CMAKE_CROSSCOMPILING)
    return()
endif()

# C++17規格の指定
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 成果物はビルドディレクトリの bin/ に出力する
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ==============================================================================
# PPA 2.0 プラグイン動的自動発見・ビルドエンジン (Zero-Touch Build Engine)
# ==============================================================================
# official_plugins/ 配下の各ペリフェラルディレクトリを自動走査し、
# fbb-plugin.json の "binary" 名と C++ ソースコードを検知してビルドターゲットを自動生成します。
# これにより、新しいペリフェラルを追加する際に CMakeLists.txt を手動修正する必要はありません。

file(GLOB PLUGIN_DIRS official_plugins/*)
foreach(PLUGIN_DIR ${PLUGIN_DIRS})
    if(IS_DIRECTORY ${PLUGIN_DIR} AND EXISTS "${PLUGIN_DIR}/fbb-plugin.json")
        file(READ "${PLUGIN_DIR}/fbb-plugin.json" MANIFEST_CONTENT)
        
        # fbb-plugin.json から "binary" 名を自動抽出しターゲット化
        string(REGEX MATCH "\"binary\":[ \t\r\n]*\"([^\"]+)\"" _MATCH ${MANIFEST_CONTENT})
        set(BINARY_NAME ${CMAKE_MATCH_1})
        
        # ディレクトリ内の C++ ソースコードを走査
        file(GLOB CPP_SOURCES "${PLUGIN_DIR}/*.cpp")
        
        if(BINARY_NAME AND CPP_SOURCES)
            # 基底クラス (I2cSlave / SpiSlave / UartDevice) を自動判別して適切なヘルパーをリンク
            file(READ "${CPP_SOURCES}" CPP_CONTENT)
            set(HELPER_SRC "common/i2c_slave.cpp")
            if(CPP_CONTENT MATCHES "SpiSlave")
                set(HELPER_SRC "common/spi_slave.cpp")
            elseif(CPP_CONTENT MATCHES "UartDevice")
                set(HELPER_SRC "common/uart_device.cpp")
            endif()

            if(NOT TARGET ${BINARY_NAME})
                add_executable(${BINARY_NAME} ${CPP_SOURCES} ${HELPER_SRC})
                target_link_libraries(${BINARY_NAME} PRIVATE Threads::Threads rt)
            endif()
        endif()
    endif()
endforeach()
