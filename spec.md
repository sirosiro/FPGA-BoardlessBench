# Project: FPGA-BoardlessBench (F-BB) Specification

## 1. プロジェクトの核心的意図 (Core Intent)
本プロジェクトの目的は、Xilinx Zynq等のFPGA SoC開発において、物理的なボードへの依存を最小化し、ソフトウェア・エンジニアがLinux環境でFPGAロジックを含めた開発・デバッグを完結させることにある。
Linuxのシステムコール・フッキング技術を用いてハードウェアの振る舞いを抽象化し、「ハードウェアの完成を待ったり、高価な評価ボードを共有したりすることなく、ソフトウェアのロジック検証を回す」ことを究極の意図とする。

---

## 2. ライセンス戦略と設計モード (License Strategy)
- **Mode:** Hybrid-Bridge Mode (デフォルト適用)
- **方針:** 既存の標準的なLinuxカーネルインターフェース（uio, i2c-dev）との互換性を保ちつつ、エミュレーション・ロジック自体は完全に独立したクリーンな実装とする。

---

## 3. システム構成の整理と実機対比 (System Architecture Mapping)

本プロジェクトの仮想環境と、実際の物理ハードウェア環境の対応関係を以下に定義する。

### 3.1. コンポーネント対応表

| コンポーネント | プロジェクト内での役割 | 実機での相当品 | 物理実体 (コード/パス) |
| :--- | :--- | :--- | :--- |
| **インターセプト・シム** | システムコールを横取りする「窓口」 | **Linux カーネル・ドライバ** | `libfpgashim.c` (DTSより自動生成) |
| **RTL シミュレーション** | 回路ロジックを実行する「実体」 | **FPGA PL (回路)** | `vfpga_top.v` (DTSより自動生成) |
| **バックエンド管理** | 共有メモリを確保し全体を繋ぐ | **AXI Bus / メモリマップ** | `vlogic_controller.py` (DTS駆動) |
| **ダッシュボード** | レジスタの状態を可視化する | **JTAG / ロジックアナライザ** | `dashboard/server.js` |
| **ビルド・テスト環境** | アプリの構築と動作検証 | **SDK / 評価ボード** | `Makefile`, `tests/scenarios/*/Makefile`, `run_tests.sh` |

### 3.2. アーキテクチャ図 (Visual Overview)

```mermaid
graph TD
    subgraph "User Space (App Side)"
        App["App (ELF Binary)"] -- "open / ioctl / mmap" --> Shim["Interception Shim (libfpgashim.so)"]
    end

    subgraph "The Bridge"
        Shim -- "Redirect" --> SHM["Shared Memory File (/tmp)"]
    end

    subgraph "Backend (Logic Side)"
        SHM <--> Sim["RTL Simulation (Verilated C++)"]
        SHM <--> Controller["Backend Manager (Python)"]
    end

    subgraph "Observation"
        Controller -. "Monitor" .-> Dashboard["Real-time Dashboard (Web UI)"]
        Dashboard ===> User((User / Developer))
    end
```

---

## 4. 各コンポーネントの機能詳細 (Component Specifications)

### 4.1. Syscall Interceptor Layer (Shim)
- **責務:** アプリケーションが発行するデバイスアクセス（`open`, `ioctl`, `mmap`）を [LD_PRELOAD](AddInfo_LD_PRELOAD.md) でインターセプトし、仮想デバイスへリダイレクトする。
- **現状:** `/dev/fpga*`, `/dev/uio*`, `/dev/i2c-*`, `/dev/mem`, `/dev/ttyUL*`, `/dev/ttyPS*` をトラップ。
- **/dev/mem 対応:** 全デバイス共通の統合物理アドレス空間を管理し、`mmap` 時に適切な共有メモリ上のオフセットへ自動でリダイレクトする。UIO等のデバイスファイル名（`label`）がDTSに明示的に定義されていないノードへの直接アクセスであっても、物理アドレスベースで透過的にルーティングを保証する。
- **生成:** `tests/vfpga_config.dts` をソースとし、`scripts/gen_vfpga.py` により自動生成される。
- **原則:** Device Tree Source (FPGAのレジスタアドレスや定義) を唯一の真実 (Source Truth) とし、Shim (Cコード) と RTL (Verilog) の一貫性を自動的に担保する。
- **意義:** FWアプリケーションのソースコードを一切変更することなく作成したelfを実機(評価ボード)で実行できる。


### 4.2. Shared Memory Register Emulator (Virtual Logic Space)
- **責務:** 共有メモリ・ファイル（標準：`/tmp`）を用いて FPGA 上の物理アドレス空間を再現する。
- **統合設計:** 複数の UIO デバイスが存在する場合、それらを包含する単一の共有メモリ空間が確保される。各デバイスへのアクセスは、物理アドレスから最小ベースアドレスを差し引いたオフセットとして SHM 内で処理される。
- **安定性:** コンテナ環境等の隔離された名前空間でも確実に動作するよう、OS標準のテンポラリ・ディレクトリを通信路に採用。

### 4.3. Logic Visualization & Diagnostic Tool (The Dashboard)
- **責務:** 共有メモリ内のレジスタ状態をリアルタイムで監視・可視化する。
- **アクセス:** `node dashboard/server.js` を起動し、ブラウザから **`http://127.0.0.1:8080`** へアクセスする（VS Code 環境では自動的にポート転送される）。
- **意義:** 実機のデバッガを繋ぐことなく、ブラウザから内部状態を把握できる。

### 4.4. RTL-C++ Integrated Bridge (Verilator Interface)
- **責務:** HDL論理を [Verilator](AddInfo_verilator.md) で高速な C++ モデルとして実行し、共有メモリの物理アドレス空間と信号を同期させる。
- **生成:** DTS の `registers` 定義に基づき、バス・インターフェースを備えた Verilog スケルトン (`vfpga_top.v`) が自動生成される。
- **物理アドレスバス:** `vfpga_top.v` の `addr` バスには、DTS で定義された **32bit 物理アドレス** が直接供給される。RTL 内の読み書きロジックは、この絶対アドレスを用いてレジスタを識別する。
- **生成制約 (ハードコードの禁止):** 自動生成されるC++ラッパー（`sim_main.cpp`）内で、特定の内部レジスタ（`top->RST` 等）を直接操作してはならない。共有メモリからの変更は、必ず汎用的なバスアクセス（`addr`, `w_data`, `w_en` 等）に変換してVerilogモジュールに伝達する。

### 4.5. UART-to-TCP Bridge (Pseudo Terminal Interface)
- **責務:** ターミナル操作が必要な UART 通信を TCP ポートへブリッジする。
- **仕組み:** Shim が `/dev/ttyUL*` や `/dev/ttyPS*` のアクセスを検知すると PTY (Pseudo Terminal) を作成。Python バックエンドがその PTY と TCP ソケット (Port 2000~) を双方向に中継する。
- **安定性:** ファームウェアの再起動に伴う PTY パスの変更を動的に検知し、ブリッジを自動再構築する機能を備える。

---

## 5. シナリオディレクトリの構成仕様 (Scenario Structure)

各テストシナリオ（`tests/scenarios/*/`）は、以下のファイルで構成されなければならない。

| ファイル | 必須 | 役割 |
| :--- | :---: | :--- |
| **README.md** | 必須 | アーキテクチャ図（Mermaid）、学習のポイント、ハードウェア仕様の解説。 |
| **Makefile** | 必須 | アプリケーションのビルド定義。`CC ?= gcc` を用い、実機移植性を担保する。 |
| **run.sh** | 必須 | 実行エントリーポイント。`scenario_runner.sh` を呼び出し、環境を立ち上げる。 |
| **config.dts** | 必須 | ハードウェア構成（物理アドレス、レジスタ名、バスID）の単一情報源。 |
| **main.c** | 必須 | アプリケーションのメインソースコード。解説コメントを必須とする。 |
| **vfpga_top.v** | 任意 | カスタムRTLロジック。存在しない場合はスケルトンが使用される。 |

---

## 6. 成立条件と不変条件 (Invariants & Pre-conditions)
- **透過性:** アプリケーション層からは、通信相手が実デバイスかシミュレータかを意識させないこと。
- **完全性:** 共有メモリ上のレジスタ操作はすべてログに記録され、デバッグの再現性が担保されていること。
- **一貫性:** シミュレーション環境で作成された ELF ファイル（FW）は、そのまま実機環境へ持ち込めること。

---

## 7. 実際の進捗まとめ (Implementation Progress)
- **Phase 1-2 完了**: 開発環境構築および `LD_PRELOAD` による最小構成の Shim 動作確認。
- **Phase 3 (Advanced) 完了**:
    - [x] **I2C インターセプト**: マルチバス対応、`bus_id` による個別識別。
    - [x] **Verilator 統合**: RTL モデルの C++ 化と共有メモリ同期の実現。
    - [x] **ダッシュボード構築**: リアルタイム Web UI によるレジスタ監視の実現。
- **Phase 4 (Hardware Transparency) 完了**:
    - [x] **Universal Intercept**: `/dev/mem` および物理アドレスベースのルーティング。
    - [x] **UART Bridge**: PTY リダイレクトによる TCP コンソール接続。
- **Phase 5 (Robustness & Refactoring) 完了**:
    - [x] **エンジンのモジュール化**: `gen_vfpga.py` のクラスベース刷新による拡張性確保。
    - [x] **通信パスの堅牢化**: `/tmp` への移行による隔離環境での安定動作。
    - [x] **双方向ステートフル同期**: ソフトウェアと RTL 間のレースコンディションの解消。
- **Phase 6 (Peripheral Expansion & UX)** 完了:
    - [x] **AXI GPIO エミュレーション**: `xlnx,xps-gpio` デバイスの認識と双方向レジスタのサポート。
    - [x] **ダッシュボード強化**: Node.js サーバーのマニフェスト動的ロード実装、および GPIO 状態（LED/トグルスイッチ）のリアルタイム可視化。
- **Phase 7 (SoC-Scale Integration & UI Polish)** 完了:
    - [x] **118ピン GPIO インターフェース**: RTL トップを SoC 規模のデザインに対応可能な 118 ピン標準へ拡張。
    - [x] **デュアルチャネル GPIO サポート**: AXI GPIO の Channel 2 (DATA2/TRI2) の自動検知と操作に対応。
    - [x] **UX 刷新**: ダッシュボードのペイン間リサイズ、トグルスイッチ UI、フォント調整等の視覚的改善。
    - [x] **プロセス管理強化**: `start_lab.sh` による Node.js プロセスの確実なキル処理の実装。
- **Phase 8 (Macro-level Observability)** 完了:
    - [x] **Register State Tracer**: レジスタ値の変化を検知し、最大500件のスナップショットを履歴保持。
    - [x] **波形可視化**: 正規化表示を用いた時系列グラフ表示機能（recharts）の導入。
    - [x] **IDE スタイル UI**: フレキシブルなペインリサイズ構造への刷新。

---

## 8. 既知の未解決課題 (Known Open Issues)
- **割り込み通知の遅延:** WSL2上のプロセス間通信による物理割り込みとの時間的乖離。
- **マルチプロセス競合:** 同一レジスタ空間への複数アプリからのアクセス排他制御。
- **I2C 詳細エミュレーション:** 現在のダミー応答から、RTL モデルや仮想デバイスへの実データ接続への拡張。
