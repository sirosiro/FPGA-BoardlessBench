# P01_frdmIMX: i.MX95 / i.MX 8M Plus HAL シナリオ

このディレクトリは、i.MX95 FRDM評価ボードおよびi.MX 8M Plus評価ボードを模擬し、C++ HAL（ハードウェア抽象化レイヤー）を用いたデバイス制御アプリケーションの開発・テストを行うシナリオです。

---

## 実行方法 (Execution)

### 1. 事前準備 (Prerequisites)
本シナリオはシミュレーション環境における OpenGL ES / EGL を用いた画像処理（歪み補正・合成）の検証を行うため、ビルド時に Mesa の EGL/GLES 開発用ヘッダーファイルが必要になります。

開発コンテナ環境内でビルドする前に、以下のコマンドを実行して必要なライブラリをインストールしてください（`run.sh` 起動時に自動チェック・インストールも行われます）。

```bash
apt update && apt install -y libegl1-mesa-dev libgles2-mesa-dev
```

> [!NOTE]
> **EGL/OpenGL ES のソフトウェアフォールバックについて**
> 物理的な GPU ハードウェアやディスプレイ表示（X11/Wayland等）を持たないコンテナやシミュレータのホスト環境では、GPU を用いた EGL 初期化が失敗します。
> これを回避するため、ホスト上では環境変数 `FORCE_MESA_FALLBACK=1` を設定し、Mesa のサーフェスレス・ソフトウェアレンダラー（`llvmpipe`）に強制的にフォールバックさせる必要があります。
>
> 付属の `run.sh` スクリプトを使用する場合は、この環境変数が自動的に設定（`export FORCE_MESA_FALLBACK=1`）されるようになっています。手動でバイナリ（`test_bin`）を直接起動する場合は、事前に環境変数を設定してください。


### 2. 実行手順 (Run Steps)
実行スクリプトにSoC名を指定して実行します。適切なDTSのロードとファームウェアのビルド・実行が自動で行われます。

* **i.MX95 環境でテストする場合:**
  ```bash
  ./run.sh imx95
  ```

* **i.MX 8M Plus 環境でテストする場合:**
  ```bash
  ./run.sh imx8mp
  ```

シミュレーション実行後、GTKWave等の波形確認ツールを用いて `vfpga.vcd` で動作を確認できます。

---

## SoCおよび評価ボードの自動検知エミュレーション (Auto-detection Emulation)

本シナリオでは、実機でのデバイスツリー（Device Tree）を用いた自動検知の仕組みを模擬するため、以下のエミュレーション機構を実装しています。

1. **SoCタイプの自動検知 (`compatible` エミュレーション)**:
   - C++ HAL（`detectSocType()`）は、Linux標準の `/sys/firmware/devicetree/base/compatible` を読み出して、現在起動しているSoC（`imx95` または `imx8mp`）を自律判定します。
   - F-BBは、DTSの最上位ノードから `compatible` プロパティを抽出し、ヌル文字（`\0`）区切りのバイナリファイル `/tmp/fbb_compatible` を自動生成します。Shimライブラリがこのファイルへのアクセスを透過的にリダイレクトします。

2. **評価ボード名のエミュレーションとダッシュボード表示 (`model` エミュレーション)**:
   - Linux標準の `/sys/firmware/devicetree/base/model` から評価ボードモデル名（例: `"NXP i.MX95 15X15 FRDM board"`) を読み出すエミュレーションを行います。
   - ジェネレータは、DTSの最上位ノードから `model` プロパティを抽出し、`/tmp/fbb_model` に書き込みます。また、`board_manifest.json` の `"model"` フィールドを介して React ダッシュボードに引き渡し、最上部ヘッダー（`[Reset Layout]` ボタンの右隣）に詳細な評価ボードモデル名を綺麗に表示します。

---

## 開発の背景と極性変換設計（Why We Do This）

本シナリオでは、**「実機での焼損防止」**と**「シミュレータ環境（ダッシュボード）での正しい表示・操作」**を**100%透過的（ソースコード変更なし）**に両立させるための特殊なレジスタマッピング設計を採用しています。

### 1. SoC間のGPIO方向制御レジスタの極性不一致
i.MX95（NXP）とZynq（Xilinx / F-BBデフォルト）では、GPIOピンの「入力/出力」を制御する方向レジスタのビット極性が完全に逆になっています。

| SoC・環境 | レジスタ名 | 極性仕様 |
| :--- | :--- | :--- |
| **i.MX95 / i.MX8MP (NXP)** | `GDIR` | **`1` = 出力 (Driven)**<br>**`0` = 入力 (Hi-Z)** |
| **Zynq / F-BB Dashboard** | `TRI` | **`1` = 入力 (Hi-Z/Tristate)**<br>**`0` = 出力 (Driven)** |

### 2. 実機焼損のリスクとバイナリ透過性のトレードオフ
> [!CAUTION]
> **実機焼損の危険性**
> もし、ダッシュボードの表示に合わせるためにC++ HALやアプリケーションファームウェアの極性を反転（`1`を「入力」、`0`を「出力」）してビルドした場合、そのバイナリを実機に書き込むと本来「入力（Hi-Z）」であるべきピンが「出力」として駆動され、接続された外部回路と衝突して物理的なボードが短絡・焼損する危険があります。

F-BBの設計哲学である**「シミュレーションと実機で完全に同一のバイナリを動かす（透過性）」**を維持するため、ソフトウェア側には一切の変更（`#ifdef SIMULATION`等のマクロ分岐含む）を加えずに解決する必要があります。

### 3. RTL層での極性変換設計による解決
この不一致を解決するため、ハードウェアのシミュレーション層（DTS定義およびRTLコード）で極性を動的に変換します。

* **仮想レジスタの定義:**  
  [imx95_config.dts](imx95_config.dts) および [imx8mp_config.dts](imx8mp_config.dts) で、ダッシュボード監視用の仮想方向レジスタとして `TRI`（オフセット `0x08`）を定義します。
* **RTLでの反転処理:**  
  [vfpga_top.v](vfpga_top.v) の中で、`TRI` (アドレス `0x08`) の読み出しに対して **`~GDIR`（GDIRのビット反転値）** を返すように記述しています。

```mermaid
sequenceDiagram
    participant App as C++ HAL (Firmware)
    participant SHM as Shared Memory (mmap)
    participant Sim as Verilator Simulation
    participant RTL as vfpga_top.v (RTL)
    participant Dash as Web Dashboard

    Note over App: setPinDirection(i, true)<br/>Write 1 (Output) to GDIR (0x04)
    App->>SHM: Write 1 to shm[1] (GDIR)
    SHM->>Sim: Update detected
    Sim->>RTL: Write GDIR = 1
    
    Note over Dash: Wants direction status
    Dash->>SHM: Read shm[2] (TRI)
    Sim->>RTL: Read address 0x08 (TRI)
    Note over RTL: TRI = ~GDIR (Inverts 1 to 0)
    RTL-->>Sim: Returns 0
    Sim-->>SHM: Write 0 to shm[2] (TRI)
    SHM-->>Dash: Read 0 (Output / LED)
    Note over Dash: Renders Pin as Green LED!
```

これによって、C++ファームウェアは実機と全く同じNXP仕様（`1` = 出力、`0` = 入力）で動作しながら、Webダッシュボード側では正しくLED（出力）およびトグルスイッチ（入力）が自動判定されてレンダリングされます。

### 4. シミュレーション時の入力競合（レースコンディション）対策
シミュレータ上でダッシュボードからトグルスイッチ（入力ピン）を切り替えて値を注入する際、シミュレーション内に接続先のない `l_pins_i` (常に0) が接続されていると、ダッシュボードが共有メモリに書き込んだ入力値が即座に `0` に上書きされてしまう問題があります。

これを防ぐため、[vfpga_top.v](file:///workspaces/FPGA-BoardlessBench-main/tests/scenarios/P01_frdmIMX/vfpga_top.v) 内の `DATA` レジスタ (オフセット `0x00`) の読み出しロジックは、シミュレーション時にダッシュボードからの注入値とファームウェアからの書き込み値を競合させず透過的に保持するよう、データレジスタ `DR` から直接読み出す簡略化設計としています。
