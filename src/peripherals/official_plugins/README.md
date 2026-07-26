# F-BB 公式ペリフェラル・プラグイン (Official Peripheral Plugins)

本ディレクトリは、FPGA-BoardlessBench (F-BB) が標準で提供する公式ペリフェラル・プラグインを配置するディレクトリです。

各プラグインは独立したフォルダ（`<vendor>_<model>` 形式）として構成されており、マニフェスト（`fbb-plugin.json`）、デバイスツリー定義（`.dtsi`）、およびエミュレータソースコード（`.cpp`）が一箇所に集約されています。

---

## 1. ディレクトリ構造

```text
src/peripherals/official_plugins/
├── generic_uart_loopback/
│   ├── fbb-plugin.json
│   └── uart_loopback.cpp
├── microchip_at24c02c/
│   ├── fbb-plugin.json
│   ├── at24c02c.dtsi
│   └── i2c_eeprom.cpp
├── microchip_mcp3208/
│   ├── fbb-plugin.json
│   ├── mcp3208.dtsi
│   └── spi_adc.cpp
├── solomon_ssd1306/
│   ├── fbb-plugin.json
│   ├── ssd1306.dtsi
│   └── ssd1306.cpp
└── winbond_w25q128/
    ├── fbb-plugin.json
    ├── w25q128.dtsi
    └── spi_flash.cpp
```

---

## 2. プラグイン構成ファイルの説明

| ファイル名 | 役割 |
| :--- | :--- |
| **`fbb-plugin.json`** | プラグインメタデータ定義ファイル（ベンダー情報、`compatible` 名、起動バイナリ名、パラメータテンプレート、ダッシュボード用UIウィジェット構成）。 |
| **`*.dtsi`** | デバイスツリー・インクルードファイル（テストシナリオの `config.dts` から `#include` して使用）。 |
| **`*.cpp`** | C++17 で実装された仮想ペリフェラルデーモンのソースコード。基底クラス（`I2cSlave`, `SpiSlave`, `UartDevice`）を継承。 |

---

## 3. 公式プラグイン一覧

1. **`generic_uart_loopback`**: Generic UART ループバック・デバイス
2. **`microchip_at24c02c`**: Microchip 24C02C 256B I2C EEPROM
3. **`microchip_mcp3208`**: Microchip MCP3208 12-bit 8-Ch SPI ADC
4. **`solomon_ssd1306`**: Solomon Systech SSD1306 128x64 I2C/SPI OLED Display
5. **`winbond_w25q128`**: Winbond W25Q128 16MB SPI NOR Flash
