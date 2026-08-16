# F-BBにおけるGDBデバッグ活用ガイド (AddInfo_GDB.md)

**FPGA-BoardlessBench (F-BB)** は、物理的な JTAG プローブ機器や専用ドライバを一切必要とせず、標準の Linux デバッガである **GDB (GNU Debugger)** や **VS Code のデバッグ機能** を用いて、実機さながらのステップ実行・レジスタ/メモリ監視を机上で行うことができます。

本ドキュメントでは、F-BB 上で Aコア (Linux FW)、Mコア (ベアメタル/RTOS)、Verilator (C++ RTLシミュレータ) を GDB でデバッグする具体的な手順と実践的な手法について解説します。

---

## 1. なぜ F-BB では GDB デバッグが圧倒的に容易なのか

実機基板上のデバッグでは、高価な JTAG プローブ（Xilinx Platform Cable USB、Segger J-Link 等）の接続、OpenOCD や GDB Server のデーモン起動、USB ドライバー認識トラブルなど、多くの物理的ハードルが存在します。

F-BB では、すべてのコンポーネント（Aコア Linux アプリケーション、C-Shim、Verilator C++ シミュレーションコア、Mコア ファームウェア）が **ホスト Linux (DevContainer) 上のネイティブプロセス** として動作します。そのため、物理 JTAG 機器を一切介さず、Linux 標準の `ptrace` 機構を通じて GDB を直接アタッチし、ネイティブスピードで高度なデバッグを実行できます。

---

## 2. 3 つの GDB デバッグパターン

### パターン 1: コマンドライン GDB による Aコア Linux FW デバッグ

C-Shim 共有ライブラリ (`libfpgashim.so`) を `LD_PRELOAD` 経由で適用しながら、対象のアプリケーションを GDB の配下で起動します。

```bash
# 1. デバッグビルドが完了しているシナリオディレクトリに移動
cd tests/scenarios/01_standard_uio

# 2. LD_PRELOAD を付与して GDB を起動
LD_PRELOAD=../../build/libfpgashim.so gdb ./app_main

# 3. GDB 内での基本操作
(gdb) break main             # main関数にブレークポイントを設定
(gdb) break read_sensor      # 任意の制御関数にブレークポイントを設定
(gdb) run                    # アプリケーションの実行開始
(gdb) next                   # ステップオーバー (1行実行)
(gdb) step                   # ステップイン (関数内に入る)
(gdb) print *reg_ptr         # 仮想FPGAレジスタポインタの値を表示
(gdb) x/10xw 0x40001000      # 共有メモリ(仮想MMIO空間)のメモリダンプ表示
(gdb) continue               # 処理の再開
```

---

### パターン 2: 異種マルチコア (AMP) の同時デュアル GDB デバッグ

Aコア (Linux) と Mコア (FreeRTOS / ThreadX / Rust ベアメタル) が共有メモリ経由で通信するヘテロジニアス SoC の開発では、2 つのターミナルからそれぞれのプロセスに独立して GDB をアタッチすることで、**両コアのハンドシェイク挙動を1つの画面で同期デバッグ** できます。

```bash
# 【ターミナル 1: Aコア Linux 制御プロセスのデバッグ】
cd tests/scenarios/10_amp_mcore_freertos
LD_PRELOAD=../../build/libfpgashim.so gdb ./app_acore

# 【ターミナル 2: Mコア RTOS ファームウェアのデバッグ】
cd tests/scenarios/10_amp_mcore_freertos
gdb ./mcore_fw.elf
```

1. ターミナル 1 (Aコア) で共有メモリメッセージ送信直前に `break` を設定。
2. ターミナル 2 (Mコア) でメッセージ受信割り込み/ループ処理に `break` を設定。
3. Aコアをステップ実行して共有メモリへ書き込みを行った瞬間、Mコア側でデータが正しく届くかを相互に監視可能です。

---

### パターン 3: VS Code 上でのグラフィカル・視覚的デバッグ

DevContainer 環境内の VS Code から、マウス操作によるブレークポイント設定や変数・メモリ・スタックトレースの視覚的デバッグが行えます。

#### `.vscode/launch.json` の設定例

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "F-BB: Debug A-Core App",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/tests/scenarios/01_standard_uio/app_main",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}/tests/scenarios/01_standard_uio",
      "environment": [
        {
          "name": "LD_PRELOAD",
          "value": "${workspaceFolder}/build/libfpgashim.so"
        }
      ],
      "externalConsole": false,
      "MIMode": "gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ]
    }
  ]
}
```

VS Code の「実行とデバッグ」タブから `F-BB: Debug A-Core App` を選択して `F5` キーを押すだけで、ソースコードの行番号をクリックして直感的にデバッグできます。

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
