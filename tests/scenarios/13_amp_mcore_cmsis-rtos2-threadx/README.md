# 13_amp_mcore_cmsis-rtos2-threadx: CMSIS-RTOS2 API と ThreadX を用いたマルチコア (AMP) 協調動作

このシナリオでは、Mコア（Coprocessor）側で標準的な抽象化APIである **CMSIS-RTOS2 API** を使用し、下層のリアルタイムOSカーネルとして **ThreadX**（Linux POSIX ポート版）を組み合わせた構成の動作検証を行います。

Aコア（Linux側アプリケーション）からの処理要求（コマンド）を仮想FPGAレジスタ経由で受信・キューイングし、OSのマルチタスク間連携のもとで演算処理を行って結果をAコアへ返す、本格的なマルチコア（AMP）協調システムを検証します。

---

## アーキテクチャ概念図

```mermaid
graph TD
    subgraph "A-Core (Linux / Host Space)"
        App["main.c (A-Core App)"]
        Bash["run.sh (Shell Commands)"]
    end

    subgraph "F-BB 仮想基盤 (Shim / Daemon)"
        Shim["libfpgashim.so (Path Interceptor)"]
        Daemon["vlogic_controller.py (Python Daemon)"]
        Sysfs["/tmp/fbb/sys/class/remoteproc/remoteproc0/\n(state, firmware, pid)"]
    end

    subgraph "M-Core (CMSIS-RTOS2 / ThreadX)"
        FW["mcore_rtos.elf (CMSIS App)"]
        subgraph "CMSIS-RTOS2 API Layer"
            CMSIS["cmsis_os2.h / cmsis_os2.c\n(stm32-mw-cmsis-rtos-tx Wrapper)"]
        end
        subgraph "ThreadX Threads"
            T1["vTaskFPGAController (osPriorityNormal)"]
            T2["vTaskDataProcessor (osPriorityNormal1)"]
            Queue["xQueue (CMSIS Message Queue)"]
        end
    end

    subgraph "RTL Hardware Simulator"
        Verilator["vfpga_sim (Verilator)"]
        SHM["/tmp/vfpga_reg (Shared Registers)"]
    end

    App & Bash -->|"/sys/class/remoteproc/..."| Shim
    Shim -->|Path Redirect| Sysfs
    Daemon -->|Polls state file| Sysfs
    Daemon -->|Spawns| FW
    
    T1 -->|Reads REG_CMD & REG_DATA_IN| SHM
    T1 -->|Acknowledge REG_CMD = 0| SHM
    T1 -->|osMessageQueuePut| Queue
    Queue -->|osMessageQueueGet| T2
    T2 -->|Calculates result * 2| T2
    T2 -->|Writes REG_DATA_OUT & REG_STATUS| SHM
    
    App -->|Writes REG_DATA_IN & REG_CMD| SHM
    App -->|Polls REG_STATUS| SHM
```

---

## `mcore_rtos.c` 共通ソースコードの同期メカニズム

本シナリオのMコアファームウェア `mcore_rtos.c` は、**シナリオ12 (`12_amp_mcore_cmsis-rtos2-freertos`) と完全に同一のソースコード**を使用します。

### タイムスタンプ比較による自動同期とフォールバック設計
12と13のそれぞれに `mcore_rtos.c` の実ファイルを配置しつつ、開発時の更新漏れや片方の削除に耐えられるよう、CMakeによるビルド構成時に以下の同期ロジックが自動実行されます：
1. **両方のファイルが存在する場合**: シナリオ12とシナリオ13の `mcore_rtos.c` の最終更新タイムスタンプを比較し、**より新しい（更新された）ファイルを自動的もう一方へ上書きコピー（同期）**します。これにより、どちらのディレクトリのファイルを編集しても自動的に同期が維持されます。
   * **無限ループ防止策**: 同期時のタイムスタンプ更新による「無限ビルド同期ループ（無限ビルドピンポン）」を防ぐため、コピー処理の前にファイル内容の差分（`compare_files`）を検出し、**実際に内容に差異がある場合のみ**上書きコピーを実行します。
2. **片方のファイルが失われている場合**: もう一方のディレクトリが存在する限り、残っている側の `mcore_rtos.c` を欠損している側へ自動的にコピーして自己修復します。
3. **相手のディレクトリ自体が削除されている場合**: コピー処理をスキップし、本ディレクトリ内にあるローカルの `mcore_rtos.c` を使ってビルドを継続します。

これにより、ポータビリティの担保（同一コードでの複数RTOS動作）と、各テストケースの独立性（片方削除時の動作継続性）を両立しています。

---

## シナリオの仕組みと特徴

1. **CMSIS-ThreadX 移植による標準APIの利用**:
   - STMicroelectronicsが提供する [stm32-mw-cmsis-rtos-tx](https://github.com/STMicroelectronics/stm32-mw-cmsis-rtos-tx) ラッパーを利用し、標準の `osKernelInitialize`, `osThreadNew`, `osMessageQueueNew` などの汎用APIを用いてマルチタスクおよびキュー通信を構築しています。これにより、特定のOS（ThreadX）の独自APIへの依存をなくしています。

2. **非同期マルチタスク協調設計**:
   - **`vTaskFPGAController` (周辺監視スレッド)**: 
     Aコアからの処理コマンド（`0xA1`）を検知すると、`REG_DATA_IN` の値を読み出し、`osMessageQueuePut` を使用してメッセージキュー（`xQueue`）へ送信します。送信後、コマンドクリアのために `REG_CMD` を `0` にリセットします。
   - **`vTaskDataProcessor` (演算処理スレッド)**: 
     キューにデータが届くまで `osMessageQueueGet` でブロック状態で待機します。データ受信後、擬似ディレイを挟んでデータを2倍にし、`REG_DATA_OUT` に結果、`REG_STATUS` に `0x01` (READY) を書き込んでAコアに完了を通知します。

3. **POSIXエミュレーション特有のメモリ設計（Linuxポート）**:
   - ThreadXのLinux/POSIXポートは、内部でLinuxのネイティブスレッド (`pthread_create`) を生成してThreadXスレッドをエミュレートします。
   - POSIXスレッドの動作を許容するため、CMakeLists.txtで管理メモリサイズをデフォルトの64KBから `TX_LINUX_MEMORY_SIZE=1048576` (1MB) に、CMSISのスタック・ヒープ用バイトプールサイズを `RTOS2_BYTE_POOL_STACK_SIZE=262144` (256KB) / `RTOS2_BYTE_POOL_HEAP_SIZE=262144` (256KB) に拡張するマクロ設定を追加し、リソース割り当て失敗によるハングアップを回避しています。

4. **アセンブラ依存コードのバイパス**:
   - Linux POSIX ポート上で Cortex-M 用のアセンブラ命令（`__get_IPSR` や `__get_CONTROL` など）がビルドエラーになるのを防ぐため、ダミーの [cmsis_compiler.h](file:///workspaces/FPGA-BoardlessBench-main/tests/scenarios/13_amp_mcore_cmsis-rtos2-threadx/cmsis_compiler.h) 内でこれらを安全なダミーマクロとして定義しバイパスしています。

5. **SystemCoreClock グローバル変数の定義**:
   - CMSIS ラッパーが参照するグローバルクロック変数である `uint32_t SystemCoreClock = 1000000U;` (1MHz想定) を定義し、リンク時の未定義シンボルエラー（`undefined reference to 'SystemCoreClock'`）を解消しています。

6. **TX_MAX_PRIORITIES 定義衝突の回避（sed パッチ）**:
   - ThreadX の Linux ポート（`tx_port.h`）内で優先度の最大値が `32` にハードコードされていることにより、CMSIS が要求する優先度設定と衝突し redefinition 警告やビルドエラーが発生するのを防ぐため、CMakeクローン直後に自動で `sed` によるコメントアウト処理を適用しています。

7. **ThreadX 固有のコンパイルマクロ設定**:
   - POSIXエミュレートされた環境上で正しくスレッドが動作・管理されるよう、`TX_MAX_PRIORITIES=64`、`USE_DYNAMIC_MEMORY_ALLOCATION`、および `TX_THREAD_USER_EXTENSION=ULONG tx_thread_detached_joinable;` などのマクロを明示的にコンパイルオプションに渡しています。

---

## 学習のポイント

1. **RTOSに依存しない汎用的なファームウェア設計**:
   - CMSIS-RTOS2 APIを使用することで、ソースコードに手を加えることなく下層のRTOSカーネル（ThreadX / FreeRTOS）を切り替えても全く同じ協調動作が機能することを学びます。
2. **Aコア・Mコア間の物理レジスタによる非同期通信シーケンス**:
   - Aコアによるコマンド発行、Mコア側タスクでの検知、処理、およびステータスレジスタ経由での完了通知という、実機でもそのまま使われる基本的な非同期通信ハンドシェイクフローを習得します。
3. **POSIXスレッド上のRTOS動作制限とメモリチューニング**:
   - ホスト環境上でのシミュレーション時に、ネイティブスレッドへのマッピング要件に起因するメモリサイズ制約（バイトプール設定）およびその調整手法を学びます。

---

## 実機で動作させるための注意点（ビルド・コンパイル構成の移行）

F-BB環境（PCシミュレーション）では、テストの利便性からThreadXのPOSIXエミュレーションレイヤーを利用していますが、実機ボード（Zynqやi.MX95など）で本ファームウェアを動作させる場合は、以下の手順でビルド環境を実機用に移行します。

* **Aコア（Linux アプリ: `test_bin`）のビルド**:
   - 開発ホストPCからクロスビルドする場合は、ターゲットプロセッサに合わせたクロスコンパイラ（例: `aarch64-linux-gnu-gcc`）を使用します。
   - UIOドライバのデバイスファイル `/dev/uio0` を介してレジスタ空間にマップするため、Cソースコード（`main.c`）は実機でもそのまま無修正で動作します。

* **Mコア（ThreadX FW: `mcore_rtos.elf`）のビルド**:
   Bコア（コプロセッサ）専用のクロスコンパイラと移植レイヤーを指定します。
   1. **クロスコンパイラの使用**: Cortex-M向けなら `arm-none-eabi-gcc` を指定します。
   2. **ThreadX Portの差し替え**: シミュレータ用の `ports/linux/gnu/` を除外し、実機CPUに適したリアルPortレイヤー（例: Cortex-M4用なら `ports/cortex_m4/gnu/` など）をビルド対象に含めます。また、`TX_LINUX_MEMORY_SIZE` 定義も不要になります。
   3. **スタートアップコードとリンカスクリプト**: 実機の物理メモリ空間（TCMや共有SRAM、予約DDR領域など）に合わせてデータとコードを配置するため、実機ボード専用のリンカスクリプト（`.ld`）およびスタートアップコードをリンクします。
   4. **ソースコードの透過性**: `mcore_rtos.c` そのものは**1文字も書き換えることなく**、上記ビルド設定の差し替えだけで実機に移植することができます。

---

## コンパイラ警告の抑制とその発生原因について

本シナリオでは、64bit Linuxのシミュレーション環境特有のミスマッチにより発生する一部の警告を抑制するため、CMakeLists.txt にて `-Wno-pointer-to-int-cast`, `-Wno-int-to-pointer-cast`, `-Wno-overflow` オプションを追加しています。これらの警告が発生していた原因と抑制目的は以下の通りです。

### 1. ポインタキャスト警告 (`-Wno-pointer-to-int-cast`, `-Wno-int-to-pointer-cast`)
* **原因**: 
  ARM CMSIS-RTOS2 API および ThreadX カーネルは、スレッドやキューの識別ハンドルを 32bit の整数値（`ULONG` や `uint32_t` など）として受け渡しする設計になっています。しかし、シミュレータが動作するホスト環境（64bit Linux）ではポインタ（`void*` や `TX_THREAD*` 等）のサイズが 64bit になります。
  このため、ラッパーコード（`cmsis_os2.c`）の内部で 64bit ポインタと 32bit 整数型の間で相互キャストが行われ、GCC から「ポインタと整数のサイズ不一致」を示す警告が出力されていました。
* **抑制目的**: 
  シミュレーション環境上でのハンドル識別子の値の変換に起因するものであり、実動作（値の格納・比較）において問題はないため、ビルドログをクリーンに保つために抑制しています。なお、実機（Cortex-M 等の 32bit マイコン）向けビルドでは、ポインタも整数も共に 32bit となるため、この警告は元々発生しません。

### 2. データ変換オーバーフロー警告 (`-Wno-overflow`)
* **原因**: 
  ThreadX 用の CMSIS ラッパー（`cmsis_os2.c`）においても、スレッドフラグ処理やタイマー制御などの各種エラーハンドリングにおいて、負の整数値をとる列挙型定数（`osErrorISR` や `osErrorParameter` 等）を `(uintptr_t)` でキャストして返す処理が一部に記述されています。
  64bit環境において負数を 64bit 無符号（`uintptr_t`）でキャストすると、`18446744073709551610` のような極めて大きな値になります。これを 32bit の変数に代入する際、GCC が値の切り捨てとオーバーフローを検知し、`-Woverflow` 警告が発生していました。
* **抑制目的**: 
  最終的に変数に代入された時点で下位 32bit に正しく切り捨てられ、元のエラー値表現に復元されるため、動作上は安全であり影響はありません。サードパーティ公式コードの可読性や保守性を考慮し、ファイルを無理に書き換えることなくコンパイルオプション側で非表示にしています。

---

## 実行方法

F-BB環境において本ディレクトリに移動して、以下のスクリプトを実行してください。

```bash
./run.sh          # ビルドと実行 (自動テストが走り、AコアとThreadXマルチタスクMコアの協調がパスします)
./run.sh --clean  # ビルド成果物とログの削除
```

### 期待される出力ログの例
実行が成功すると、以下のようにMコア（`[M-Core]`）のCMSIS-RTOS2マルチタスク起動ののち、Aコア（`[A-Core]`）との間で正常にデータのやり取りが行われ、テストが成功します。

*(※ 注: Aコア側テストプログラム `main.c` の共通化表示の都合により、ログに `FreeRTOS` と出力されますが、Mコア側は実際には ThreadX が動作しています)*

```text
--- FreeRTOS AMP Multitask Test Start ---
[A-Core] Opening /dev/uio0...
[Shim M-Core] Successfully mapped 0x40000000 -> 0x40000000 (size: 4096, offset: 0)
[M-Core] CMSIS-RTOS2 firmware starting...
[M-Core] Data Processor thread started.
[M-Core] FPGA Controller thread started.
[A-Core] Writing test value 12345 to REG_DATA_IN...
[A-Core] Sending Command 0xA1 to REG_CMD...
[A-Core] Waiting for M-Core to set REG_STATUS to READY (0x01)...
[M-Core] Command 0xA1 detected via CMSIS-RTOS2 thread. Data input: 12345. Sending to processing queue...
[M-Core] Processing data: 12345...
[M-Core] Data processed. Result 24690 written to REG_DATA_OUT.
[A-Core] READY status received from M-Core.
[A-Core] Reading REG_DATA_OUT: 24690 (Expected: 24690)
[A-Core] SUCCESS: Data correctly processed via CMSIS-RTOS2!
--- FreeRTOS AMP Multitask Test Finished ---

[Runner] RESULT: SUCCESS
```
