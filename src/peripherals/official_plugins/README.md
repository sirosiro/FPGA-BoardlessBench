# F-BB 公式ペリフェラル・プラグイン (Official Peripheral Plugins) & PPA 5.1 メーカー開発マニュアル

本ディレクトリは、FPGA-BoardlessBench (F-BB) が標準で提供する公式ペリフェラル・プラグインの配置場所、および**サードパーティ・サードベンダが独自ペリフェラルを作成するための PPA 5.1 (Plug-and-Play Architecture) 標準開発マニュアル**です。

---

## 1. Zero Touch 開発コンセプト (PPA 5.1)

PPA 5.1 仕様に基づき、ペリフェラルメーカーは **`fbb-plugin.json` (マニフェスト)** と必要に応じた **`board.svg` (基板外形図)** を用意するだけで、F-BB の Web ダッシュボード上に自作ペリフェラルの対話型 GUI 画面（リアルタイムフレームバッファ表示、7セグメントLED、アナログセンサースライダー等）を **ダッシュボード側のコードを1行も改変することなく (Zero Touch)** 自動レンダリングさせることができます。また、`fbb plugin install <url>` コマンド（`fbb-cli`）によるワンコマンド流通を標準サポートしています。

---

## 2. プラグインのディレクトリ構造

各プラグインは `<vendor>_<model>` 形式の独立したフォルダとして構成されます。

```text
src/peripherals/official_plugins/
├── generic_uart_loopback/
│   ├── fbb-plugin.json
│   └── uart_loopback.cpp
├── adafruit_ht16k33/
│   ├── fbb-plugin.json
│   ├── board.svg
│   ├── ht16k33.dtsi
│   └── ht16k33.cpp
├── generic_hub75_matrix64x64/
│   ├── fbb-plugin.json
│   ├── board.svg
│   ├── hub75_matrix.dtsi
│   └── hub75_matrix.cpp
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
│   ├── board.svg
│   ├── ssd1306.dtsi
│   └── ssd1306.cpp
└── winbond_w25q128/
    ├── fbb-plugin.json
    ├── w25q128.dtsi
    └── spi_flash.cpp
```

---

## 3. マニフェスト仕様 (`fbb-plugin.json`)

ペリフェラルの UI および可視化動作は、`fbb-plugin.json` 内の `ui_widget` フィールドによって完全にデータ駆動制御されます。Web ダッシュボード上では、`board_svg`（基板ベクター画像）が設定されているペリフェラルは **`[PCB Board]` モードが初期デフォルト表示** となり、50%〜400% 拡大時のマウスドラッグによる自由な視点移動（パン機能）を標準サポートします。

### コントロール型一覧 (`ui_widget.controls`)

#### 1. ディスプレイ・フレームバッファ型 (`type: "framebuffer"`)
OLED ディスプレイ、RGB LED マトリクス、液晶パネル、カメラ映像等の画像・映像出力を描画します。

```json
{
  "ui_widget": {
    "title": "SSD1306 OLED Display (128x64)",
    "board_svg": "board.svg",
    "overlay_offset": {
      "left": "8.214%",
      "top": "16.786%",
      "width": "83.571%",
      "height": "43.571%"
    },
    "controls": [
      {
        "type": "framebuffer",
        "name": "display",
        "width": 128,
        "height": 64,
        "format": "mono_page_8",
        "palette": { "bg": "#040604", "fg": "#00ff50" },
        "render_mode": "pixelated"
      }
    ]
  }
}
```

- **`format` パラメータ**:
  - `"mono_page_8"`: SSD1306 などの 1byte=8縦ピクセルモノクロ形式
  - `"rgb24"`: HUB75 などの 1ピクセル=3bytes (R,G,B) 形式
  - `"rgba32"`: カラー液晶などの 1ピクセル=4bytes (R,G,B,A) 形式
- **`render_mode` パラメータ**:
  - `"led_matrix"`: 個別の丸型LEDドットが発光するマトリクス描画
  - `"pixelated"`: ディスプレイの平坦なドットバリュードット描画

#### 2. セグメント表示器型 (`type: "segment_array"`)
7セグメント / 14セグメント LED 表示器を描画します。

```json
{
  "controls": [
    {
      "type": "segment_array",
      "name": "display_7seg",
      "digit_count": 4,
      "has_colon": true,
      "color": "red"
    }
  ]
}
```

- **`color`**: `"red"`, `"green"`, `"blue"`, `"yellow"`, `"white"`

#### 3. アナログセンサー・スライダー型 (`type: "slider"`)
可変抵抗器、温度・照度センサー、ADC 電圧インジェクション等の入力を制御します。

```json
{
  "controls": [
    {
      "type": "slider",
      "name": "channel0",
      "label": "CH0 Analog Input (V)",
      "min": 0,
      "max": 3.3,
      "default": 1.65,
      "unit": "V",
      "shm_file": "spi_adc",
      "shm_offset": 0,
      "format": "uint16_le",
      "raw_min": 0,
      "raw_max": 4095
    }
  ]
}
```

- **`shm_file`**: 対象共有メモリファイル名（`/dev/shm/<shm_file>`）
- **`shm_offset`**: バイトオフセット
- **`format`**: `"uint16_le"`, `"uint8"`, `"float"`

---

## 4. ペリフェラルエミュレータ C++ 実装規約 & 統一 CLI インターフェース (`cli_helper.hpp`)

サードパーティ製および公式ペリフェラルデーモンは、`src/peripherals/common/` に配置された抽象基底クラス（`I2cSlave`, `SpiSlave`, `UartDevice`）を継承して実装します。

コマンドライン引数の解析およびシグナル制御には、共通パーサー `fbb::PluginCLI`（`src/peripherals/common/cli_helper.hpp`）を使用します。これにより、すべてのペリフェラルで以下の統一オプションおよび整形ヘルプメニューが自動サポートされます。

### 統一コマンドラインオプション
| オプション | 引数 | 説明 | 対象デバイス例 |
| :--- | :--- | :--- | :--- |
| `-s`, `--socket` | `<path>` | I2C / SPI マスターブリッジ用 UNIX ドメインソケットパス | OLED, 7seg, ADC, EEPROM, Flash |
| `--shm` | `<path>` | POSIX 共有メモリパス（`/dev/shm/...`） | HUB75, フレームバッファ |
| `-f`, `--file` | `<path>` | 不揮発ストレージ保存用バッキングファイルパス | EEPROM, SPI Flash |
| `-p`, `--pts-file` | `<path>` | 割り当てられた PTY スレーブパス格納ファイル | UART ループバック |
| `-i`, `--init-val` | `<val>` | レジスタまたは ADC チャンネルの初期値（16進数/10進数） | ADC, EEPROM |
| `-w`, `--width` | `<pixels>` | ディスプレイ・マトリクスの横幅ピクセル数（既定値: 64） | HUB75, OLED |
| `-H`, `--height` | `<pixels>` | ディスプレイ・マトリクスの縦幅ピクセル数（既定値: 64） | HUB75, OLED |
| `-h`, `--help` | - | プラグイン情報、パラメータ一覧、使用例を表示して終了 | 全ペリフェラル |

### C++ エミュレータ最小実装テンプレート
```cpp
#include "../../common/i2c_slave.hpp"
#include "../../common/cli_helper.hpp"

class MyCustomI2cSensor : public I2cSlave {
public:
    MyCustomI2cSensor(uint8_t dev_addr) : I2cSlave(dev_addr) {}
protected:
    void onWrite(const std::vector<uint8_t>& data) override {
        // マスターからの書き込み処理
    }
    std::vector<uint8_t> onRead(size_t length) override {
        // センサー応答バイト列の返却
        return std::vector<uint8_t>(length, 0x55);
    }
};

static MyCustomI2cSensor* g_sensor_instance = nullptr;
void handle_signal(int sig) {
    if (g_sensor_instance) g_sensor_instance->stop();
}

int main(int argc, char* argv[]) {
    fbb::PluginCLI cli("Vendor Custom I2C Sensor", "Emulates 3-axis accelerometer over I2C.");
    auto opt = cli.parse(argc, argv);
    if (opt.show_help) return 0;
    if (opt.socket_path.empty()) { cli.print_help(); return 1; }

    MyCustomI2cSensor sensor(0x68);
    g_sensor_instance = &sensor;
    fbb::PluginCLI::setup_signal_handler(handle_signal);

    return sensor.start(opt.socket_path) ? 0 : 1;
}
```

---

## 5. 基板ベクター外形図 (`board.svg`)

- 基板外形、ネジ穴、コネクタ、ICチップ、シルク文字等を SVG 形式で作成します。
- ディスプレイ画面切り抜き部分が存在する場合、`overlay_offset`（`left`, `top`, `width`, `height`）をパーセンテージ指定することで、SVG 画像の上にフレームバッファ（Canvas）がピクセル単位で 1:1 にオーバーレイ描画されます。

---

## 6. 公式プラグイン一覧

1. **`adafruit_ht16k33`**: Adafruit 0.56" 4-Digit 7-Segment HT16K33 I2C Display
2. **`generic_hub75_matrix64x64`**: Generic HUB75 64x64 / 128x64 RGB LED Matrix
3. **`generic_uart_loopback`**: Generic UART ループバック・デバイス
4. **`microchip_at24c02c`**: Microchip 24C02C 256B I2C EEPROM
5. **`microchip_mcp3208`**: Microchip MCP3208 12-bit 8-Ch SPI ADC
6. **`solomon_ssd1306`**: Solomon Systech SSD1306 128x64 I2C OLED Display
7. **`winbond_w25q128`**: Winbond W25Q128 16MB SPI NOR Flash
