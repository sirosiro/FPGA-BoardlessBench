# Scenario 17: AMP M-Core Rust Embassy

F-BB（FPGA-BoardlessBench）環境において、モダンな非同期駆動型組み込み Rust OS/ランタイムである **Embassy** の動作を検証するシナリオです。

## 概要

本シナリオは、Aコア（Linuxアプリ/C）からMコア（Embassyランタイム/Rust）に対してデータ処理要求を送り、Mコア側が非同期エグゼキュータ上で効率的に要求を処理して応答を返す様子をシミュレートします。

Mコア側（Rust）は `embassy-executor` の `arch-std` 機能を使用し、ホストOS上でマルチタスク非同期動作（`async/await`）をエミュレートします。

## ディレクトリ構成（対称設計）

本シナリオは、Aコア（C言語）とMコア（Rust）の対等な協調関係を明示するため、双方のソースコードを対称的に整理しています。

* **[a_core/](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/17_amp_mcore_Rust_embassy/a_core)**: Aコア側のC言語アプリケーション (`main.c`)
* **[m_core/](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/17_amp_mcore_Rust_embassy/m_core)**: Mコア側のRustアプリケーションとモジュール (`Cargo.toml`, `src/`)

## アーキテクチャ

```mermaid
graph TD
    App["A-Core (C)\n(test_bin)"]
    VFPGA["fbb_pac::Vfpga\n(Virtual Registers in SHM)"]
    Timer["RTL Timer\n(Verilator/vfpga)"]
    MCore["M-Core (Rust)\n(mcore_embassy)\n[Embassy Executor (std)]"]

    App --> VFPGA
    Timer <-->|"Sync"| VFPGA
    VFPGA --> MCore
```

### 1. 単一の情報源 (DTS)
本シナリオは以下の DTS 定義に基づき、自動生成された PAC（Peripheral Access Crate）および C Shim を使用します。

* **Timer レジスタ**: `timer_target`, `timer_current`, `timer_irq`
* **通信用レジスタ**: `cmd` (Aコアからのコマンド), `status` (Mコアの処理状態), `data_in`/`data_out` (データ入出力)

## ビルドと実行

ホストPC上の `cargo` を使用して依存関係を自動的にダウンロードし、ファームウェアをコンパイルします。依存ライブラリのソースはホスト側の Cargo キャッシュに格納され、本プロジェクトのリポジトリには混入しません。

```bash
# シナリオ単体での実行
./run.sh
```
