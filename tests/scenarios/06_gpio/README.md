# Scenario 06: AXI GPIO

このシナリオでは、Zynq等のFPGAでよく利用される `AXI GPIO` (General Purpose Input/Output) のエミュレーション機能のテストを行います。

## アーキテクチャ概念図

```mermaid
graph TD
    subgraph "Application Layer"
        Main["main.c (GPIO Test App)"]
    end

    subgraph "Interception Layer"
        Shim["libfpgashim.so (Shim)"]
    end

    subgraph "Emulation Layer (Shared Memory /tmp)"
        SHM["Scalable SHM (DTS Defined Range)"]
        subgraph "Register Map (Physical Address)"
            DATA["0x41200000: DATA (CH1)"]
            TRI["0x41200004: TRI (CH1)"]
            DATA2["0x41200008: DATA2 (CH2)"]
            TRI2["0x4120000C: TRI2 (CH2)"]
        end
    end

    subgraph "Visualization & Logic"
        VLogic["vlogic_controller.py / Verilator"]
        Dash["Node.js Dashboard (Port 8080)"]
    end

    Main -- "mmap(/dev/uio0)" --> Shim
    Shim -- "Route to" --> SHM
    Main -- "regs[0] = 0x01 (Output)" --> DATA
    Main -- "val = regs[2] (Input)" --> DATA2

    DATA <--> VLogic
    TRI  <--> VLogic
    
    DATA -. "Real-time Update" .-> Dash
    TRI  -. "Direction Status" .-> Dash
```

## 構成

- **`config.dts`**: AXI GPIOのデバイスツリー定義 (`xlnx,xps-gpio-1.00.a`) が含まれています。ベースアドレス `0x41200000` に配置され、DATA, TRIなどの基本的なレジスタが定義されています。
- **`main.c`**: GPIOレジスタに対してmmapを行い、チャネル1を出力、チャネル2を入力として設定してテストを行うC言語のアプリケーションです。DTS で定義された物理アドレスを基準にアクセスを行います。
- **Dashboard**: Node.jsダッシュボード側で、GPIOの状態（入力・出力ピンの値）を視覚的に確認・操作するための機能拡張のテストにも使用されます。

## 学習のポイント

1.  **UIO経由のレジスタアクセス**: `/dev/uio0` を通じて、物理アドレス `0x41200000` のレジスタをどのように操作するかを学びます。RTL 側のアドレスバス (`addr`) にもこの絶対アドレスが流れます。
2.  **TRIレジスタの役割**: GPIOのピンが「入力」か「出力」かを、`TRI` レジスタの設定によって切り替える AXI GPIO 特有の挙動を確認します。
3.  **118ピン標準インターフェース**: 内部の AXI GPIO ロジックの状態を、SoC 全体のピンを模擬する 118 ピンの `l_pins_i/o/t` 信号へどのようにマッピングするかを確認します。
4.  **リアルタイム可視化と双方向操作**: 書き込み結果がダッシュボードの **LED** に反映されるだけでなく、ダッシュボード上の **トグルスイッチ** を操作することで、シミュレーション中の入力ピンの状態をリアルタイムに変更できます。
