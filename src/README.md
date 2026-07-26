# /src - FPGA-BoardlessBench (F-BB) 実装層（コアコンポーネント）

このディレクトリには、シミュレーション・インフラのソースコードが収められています。
**「開発者が直接編集するファイル」**と**「DTSから自動生成されるファイル」**が混在しているため、以下の分類に注意してください。
※自動生成されるファイルは `make clean` によって削除されます。直接編集してもビルド時に上書きされるため、変更が必要な場合は必ずソース（DTS等）を修正してください。

## ディレクトリ構成と役割

| ディレクトリ | 役割 | 主なファイル | 生成種別 |
| :--- | :--- | :--- | :---: |
| **controller** | バックエンド管理 | `vlogic_controller.py` | 手書き |
| **peripherals** | ペリフェラル SDK & 公式プラグイン | `sdk/`, `official_plugins/` | 手書き |
| **shim** | システムコール横取り | `libfpgashim.c` | **自動生成** |
| **sim** | Verilator実行環境 | `sim_main.cpp` | **自動生成** |
| **rtl** | 回路ロジック (Verilog) | `vfpga_top.v` | **自動生成** / 手書き |
| **include** | 共有システムヘッダー | `vfpga_system_config.h` | **自動生成** |

---

## 各コンポーネントの詳細

### 1. controller (Python)
- **vlogic_controller.py**: プロジェクトの心臓部。共有メモリの初期化、RTLシミュレータの起動監視、UARTブリッジの管理、仮想 remoteproc インターフェースの自律監視に加え、**PPA (Peripheral Plugin Architecture) に基づくサードパーティ・プラグイン動的発見・自動起動エンジン（5 段階の探索パス）**および DTS 仮想ブレッドボード配線の動的アサインを行います。

### 2. peripherals (SDK & Official Plugins)
- **sdk/**: メーカーや外部開発者が自社デバイスに対応した F-BB 仮想モデルを独立リポジトリとして作成・パッケージ化するための単体配布可能な C++/Python 用 SDK です。
- **official_plugins/**: Microchip AT24C02C, Winbond W25Q128, MCP3208, Solomon SSD1306, UART Loopback 等、F-BB 公式で保守する標準プラグイン群です。各プラグイン内に標準マニフェスト (`fbb-plugin.json`) とノード定義 (`.dtsi`) を所有しています。

### 3. shim (C)
- **libfpgashim.c**: アプリケーションの `open`, `mmap`, `ioctl` をフックして共有メモリにリダイレクトします。また、仮想 remoteproc API (`/sys/class/remoteproc/...`) やファームウェアロードパス (`/lib/firmware/...`) へのアクセスを `/tmp/fbb/` 以下に透過的にリダイレクトします。
- **注意**: このファイルはビルド時に `gen_vfpga.py` によって上書きされるため、直接編集しないでください。

### 4. sim (C++)
- **sim_main.cpp**: Verilator でコンパイルされた回路を駆動するブリッジエンジンです。
- **特徴**: C++17 SFINAE 技術を用いた **動的ポート検出** を搭載しています。RTL 側に特定のバス（`addr`, `w_data` 等）が存在しない場合でも、クロックのみでシミュレーションを継続し、波形（VCD）を出力することが可能です。

### 5. rtl (Verilog)
- **vfpga_top.v**: シミュレーションのトップモジュールです。
- **標準インターフェース**: 118 ピンの標準 IO (`l_pins_i/o/t`) を備えたスケルトンとして自動生成されます。
- **柔軟な配置**: 各シナリオディレクトリに同名のファイルがある場合はそれが優先的に使用され、存在しない場合は DTS に基づく汎用スケルトンが自動配置されます。

### 6. include (C)
- **vfpga_system_config.h**: F-BBシステムシミュレーション層（C-Shimおよびシミュレータ）で使用する共有メモリやレジスタサイズ、ソケットなどの共通定義が含まれます（シナリオ固有のデバイスパス定義は、各シナリオディレクトリ配下の `vfpga_device_config.h` に分離生成されます）。

---
[README.md](../README.md) へ戻る

