# Scenario 01c_protocol_assertion

## 1. 概要 (Overview)
F-BB 次世代機能 **トランザクション・ロガー＆プロトコル・アサーション・エンジン (Transaction Logger & Protocol Assertion Engine)** の動作検証および対話的リアルタイム検査用シナリオです。

![FPGA-BoardlessBench (F-BB) AroundView Dashboard](assets/dashboard.gif)

## 2. 検証内容
`config.dts` 内でアクセストラッキング属性（`: RO`, `: WO`, `: RW`）が定義されたレジスタに対するアクセス可否を監視します。
- `STATUS @ 0x04 : RO` (Read-Only) への Write 試行 ➔ 🚨 `[PROTOCOL_VIOLATION] WRITE_TO_RO` 警告ログを出力
- `TRIG @ 0x08 : WO` (Write-Only) からの Read 試行 ➔ 🚨 `[PROTOCOL_VIOLATION] READ_FROM_WO` 警告ログを出力
- C-Shim (`libfpgashim.so`) が不正アクセスをリアルタイム検知し、標準エラー出力へのアサート警告、`/tmp/fbb_protocol_violations.log` 記録、および Web ダッシュボード **Transaction Logger** へのリアルタイム配信を行います。

## 3. 実行モードとトリガーマップ

### A. 自動テストモード (`fbb test 01c_protocol_assertion`)
ダッシュボードからの入力を待たずに自動検証を即座に実行し、成功結果を出力して約1秒で正常終了 (`exit 0`) します。

### B. 対話型ラボモード (`./start_lab.sh tests/scenarios/01c_protocol_assertion/`)
`start_lab.sh` から起動すると対話型モード (`VFPGA_INTERACTIVE=1`) が有効化され、ダッシュボードの **UART Console** にヘルプメニューが表示されます。

#### 1) GPIO Pin Array トラッキングマップ (Web ダッシュボード「GPIO / Pin Array」ペイン)
- **Pin 0 (ON)**: CTRL (`0x00`) への正常 Write 実行 ➔ Transaction Logger に `[OK] WRITE` を表示
- **Pin 1 (ON)**: STATUS (`0x04`) からの正常 Read 実行 ➔ Transaction Logger に `[OK] READ` を表示
- **Pin 2 (ON)**: Read-Only な STATUS (`0x04`) への不正 Write 試行 ➔ 🚨 `[PROTOCOL_VIOLATION] WRITE_TO_RO` を表示
- **Pin 3 (ON)**: Write-Only な TRIG (`0x08`) からの不正 Read 試行 ➔ 🚨 `[PROTOCOL_VIOLATION] READ_FROM_WO` を表示

#### 2) UART コンソールコマンド (Web ダッシュボード「UART Console」ペイン)
- `1`: CTRL (`0x00`) への正常 Write 実行
- `2`: STATUS (`0x04`) からの正常 Read 実行
- `3`: STATUS (`0x04`) への不正 Write 試行
- `4`: TRIG (`0x08`) からの不正 Read 試行
- `h`: ヘルプメニューの再表示
- `q`: 対話ループの終了
