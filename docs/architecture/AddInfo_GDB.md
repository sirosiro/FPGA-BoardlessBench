# F-BBにおけるGDBデバッグ活用ガイド (AddInfo_GDB.md)

**FPGA-BoardlessBench (F-BB)** は、物理的な JTAG プローブ機器や専用ドライバを一切必要とせず、標準の Linux デバッガである **GDB (GNU Debugger)** や **VS Code のデバッグ機能** を用いて、実機さながらのステップ実行・レジスタ/メモリ監視を机上で行うことができます。

本ドキュメントでは、F-BB 上で Aコア (Linux FW)、Mコア (ベアメタル/RTOS)、Verilator (C++ RTLシミュレータ) を GDB でデバッグする具体的な手順と実践的な手法について解説します。

---

## 1. なぜ F-BB では GDB デバッグが圧倒的に容易なのか

実機基板上のデバッグでは、高価な JTAG プローブ（Xilinx Platform Cable USB、Segger J-Link 等）の接続、OpenOCD や GDB Server のデーモン起動、USB ドライバー認識トラブルなど、多くの物理的ハードルが存在します。

F-BB では、すべてのコンポーネント（Aコア Linux アプリケーション、C-Shim、Verilator C++ シミュレーションコア、Mコア ファームウェア）が **ホスト Linux (DevContainer) 上のネイティブプロセス** として動作します。そのため、物理 JTAG 機器を一切介さず、Linux 標準の `ptrace` 機構を通じて GDB を直接アタッチし、ネイティブスピードで高度なデバッグを実行できます。

---

## 2. 3 つの GDB デバッグパターン

### パターン 1: 統合 CLI (`bin/fbb debug`) による自動協調デバッグ（推奨）

F-BB の統合 CLI ツールキット (`bin/fbb`) に実装された `debug` サブコマンドを使用することで、**「CMake デバッグビルド ➔ Verilator/コントローラのバックグラウンド起動 ➔ DTS レジスタ拡張の読み込み ➔ GDB 起動 ➔ 終了時の自動クリーンアップ」** がワンストップで完結します。

```bash
# シナリオ名または前方一致プレフィックスで即座にデバッグ開始
bin/fbb debug 01b

# 出力例:
# 🔨 [F-BB Debug] Building simulation engine & target for scenario: 01b_uio_irq_interrupt (Debug mode)...
# 🚀 [F-BB Debug] Starting simulation backend (controller & vfpga_sim)...
# 🐞 [F-BB Debug] Starting GDB on target: .../01b_uio_irq_interrupt/test_bin
# [F-BB] GDB Extension loaded for model: generic-vfpga. Type 'fbb-info' or 'fbb-regs' to inspect.
# (gdb) 
```

GDB セッション終了時（`(gdb) quit`）には、バックグラウンドで動いていたシミュレータプロセス群も自動的に綺麗に終了・回収されます。

---

### パターン 2: DTS 自動連携 GDB 拡張コマンド (`fbb-regs`, `fbb-write`)

F-BB では、`config.dts` から各シナリオ専用の GDB 拡張スクリプト（`fbb_gdb.py`）が自動生成されます。生アドレスを手動計算する必要はなく、DTS で定義されたレジスタ名で直感的に監視・変更が可能です。

```bash
# 1. DTS 定義レジスタと現在のメモリ値（16進/10進）を一覧表示
(gdb) fbb-regs

# 実行例:
# === Device: vfpga_irq_timer (uio) @ 0x40000000 (size: 0x1000) ===
#   Offset   Address      Register                 Dir   Value (Hex)    Value (Dec) 
#   --------------------------------------------------------------------------------
#   0x0000   0x40000000   CTRL                     RW    0x00000001     1           
#   0x0004   0x40000004   STATUS                   RW    0x00000001     1           
#   0x0008   0x40000008   INT_ACK                  RW    0x00000000     0           
#   0x000C   0x4000000C   CNT                      RW    0x00000005     5           

# 2. レジスタ名またはアドレスを指定して 32-bit 値を直接書き込み (シミュレータ側へ即時反映)
(gdb) fbb-write CTRL 0x0
# [F-BB GDB] Successfully wrote 0x00000000 (0) to vfpga_irq_timer::CTRL

# 3. C-Shim がインターセプトしている仮想デバイスおよび FD 一覧の確認
(gdb) fbb-fds

# 4. ターゲットモデル・ハードウェア概要の確認
(gdb) fbb-info
```

---

### パターン 3: Antigravity IDE / VS Code による「F5」ワンクリック・デバッグ

リポジトリ直下の [`.vscode/launch.json`](file:///workspaces/FPGA-BoardlessBench/.vscode/launch.json) および [`.vscode/tasks.json`](file:///workspaces/FPGA-BoardlessBench/.vscode/tasks.json) に標準デバッグ構成が配備されています。

1. エディタ上で任意のシナリオファイル（例: `tests/scenarios/01b_uio_irq_interrupt/main.c`）を開く。
2. キーボードの **`F5`** を押す（または「実行とデバッグ」タブから `F-BB: Debug Active Scenario (F5)` を選択）。
3. **事前タスク (`preLaunchTask`)** により、シミュレータエンジンとコントローラがバックグラウンドで自動起動。
4. `main` 関数の先頭で自動的にブレークポイント停止。
5. エディタの「デバッグコンソール」タブで `-exec fbb-regs` と入力すれば、GUI 画面内でそのまま DTS レジスタテーブルをリアルタイム参照可能。
6. デバッグを停止（`Shift + F5`）すると、**事後タスク (`postDebugTask`)** によりシミュレータプロセスが自動終了。

---

### パターン 4: 異種マルチコア (AMP) の同時デュアル GDB デバッグ

Aコア (Linux) と Mコア (FreeRTOS / ThreadX / Rust ベアメタル) が共有メモリ経由で通信するヘテロジニアス SoC の開発では、2 つのターミナルからそれぞれのプロセスに独立して GDB をアタッチすることで、**両コアのハンドシェイク挙動を1つの画面で同期デバッグ** できます。

```bash
# 【ターミナル 1: Aコア Linux 制御プロセスのデバッグ】
bin/fbb debug 10_amp_mcore_freertos

# 【ターミナル 2: Mコア RTOS ファームウェアのデバッグ】
cd tests/scenarios/10_amp_mcore_freertos
gdb ./mcore_fw.elf
```

1. ターミナル 1 (Aコア) で共有メモリメッセージ送信直前に `break` を設定。
2. ターミナル 2 (Mコア) でメッセージ受信割り込み/ループ処理に `break` を設定。
3. Aコアをステップ実行して共有メモリへ書き込みを行った瞬間、Mコア側でデータが正しく届くかを相互に監視可能です。

---

## 3. Verilator C++ シミュレーションコアの GDB デバッグ

F-BB では、Verilog RTL (`vfpga_top.v`) も Verilator によって C++ クラス (`Vvfpga_top.cpp`) に変換されてビルドされます。回路ロジック側の挙動や C++ ラッパー (`sim_main.cpp`) の同期処理をデバッグする場合も、同様に GDB を使用できます。

```bash
cd tests/scenarios/07_verilator_custom_ip
gdb ./sim_main

(gdb) break sim_main.cpp:45   # クロック駆動ループにブレークを設定
(gdb) print top->clk          # RTLクロック信号の状態を表示
(gdb) print top->custom_reg   # Verilogモジュール内部のレジスタ信号を表示
```

---

## 4. デバッグ実践のコツとトラブルシューティング

### ① デバッグシンボルの保持 (`-g` オプション)
ソースコードの行番号や変数名を表示するには、CMake ビルド時に Debug モードを指定します。

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

### ② Guard Page による `SIGSEGV` 発動時の捕捉
F-BB では、DTS 未定義のアドレスへ誤アクセスした際、C-Shim がガードページ (`mprotect(PROT_NONE)`) を発動させて `SIGSEGV` を発生させます。GDB 配下で実行しておくことで、不正アクセスが発生した瞬間に正確なソースコード行とスタックトレースを補獲できます。

```bash
(gdb) run
# Program received signal SIGSEGV, Segmentation fault.
# 0x00007ffff7f12345 in main () at app_main.c:42
(gdb) backtrace               # スタックトレースの表示
```

---

## 5. まとめ

F-BB のアーキテクチャ設計により、従来の実機開発で必須だった物理 JTAG プローブや複雑な接続設定を行うことなく、**普段使いの GDB や VS Code のデバッガをそのまま活用した高速なハード・ソフト協調デバッグ** が実現します。
