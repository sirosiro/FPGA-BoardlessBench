# Scenario 23: AMP M-Core SFS 詳細設計書 & 実機移植ガイド

本ドキュメントでは、`23_amp_mcore_sfs` における **実機透過なハードウェアタイマー割り込み機構**、**HW/SW 境界分離の実装原理**、および **同期・競合対策** の詳細を解説します。

---

## 1. `pthread` を使わない実機透過割り込み BSP（リンク時多態）

`simple-functions-scheduler` のオリジナルサンプル（`sample_frcc01.c`）はホスト上でタイマーを模擬するために `pthread` を使っていますが、組み込み M コアファームウェアに POSIX スレッドを持ち込むと実機透過性が損なわれます。

本シナリオでは、M コア本体（[mcore_sfs.c](mcore_sfs.c)）が抽象ヘッダ [mcore_irq_bsp.h](mcore_irq_bsp.h) のみを参照し、ビルド時にリンクする BSP を差し替える **リンク時多態（Link-Time Polymorphism）** を採用しています。

### 1.1 F-BB シミュレーション側の割り込み伝達経路

```mermaid
sequenceDiagram
    participant MCore as M-Core (mcore_sfs.c)
    participant BSP as BSP (bsp_sim_irq.c)
    participant RTL as FPGA RTL (vfpga_top.v)
    participant Sim as Simulator (sim_main.cpp)

    Note over RTL: Reset: TIMER_CTRL=0 (Timer Stopped)
    MCore->>BSP: mcore_bsp_register_timer_isr(on_timer_isr)
    MCore->>MCore: FRCInterrupt(mcore_bsp_di, mcore_bsp_ei)
    MCore->>RTL: REG_TIMER_PERIOD=500; REG_TIMER_CTRL=1
    loop Every TIMER_PERIOD Clocks
        RTL->>RTL: TIMER_STATUS[0]=1, mcore_irq_out=1
        RTL->>Sim: mcore_irq_out 立ち上がりエッジ (0 -> 1)
        Sim->>BSP: kill(remoteproc0_pid, SIGUSR1)
        BSP->>MCore: on_timer_isr() 非同期実行
        MCore->>MCore: gFreeRunCounter++
        MCore->>RTL: REG_TIMER_INT_ACK = 1 (0x4000000C)
        Sim->>RTL: Write ACK -> TIMER_STATUS[0]=0, mcore_irq_out=0
    end
```

1. **コンパイル時ポート有無判定（C++17 SFINAE トレイト `has_mcore_irq_out<T>`）**:
   - [sim_traits.h](../../../src/include/sim_traits.h) および [generator_rtl.py](../../../scripts/vfpga/generator_rtl.py) に `has_mcore_irq_out<T>` を実装しています。
   - `vfpga_top.v` が `mcore_irq_out` 出力ポートを持つシナリオのビルド時だけ有効化され、持ち合わせない既存シナリオにはコンパイル負荷も実行時オーバーヘッドも一切与えません。
   - `sim_main.cpp` が `mcore_irq_out` の立ち上がりエッジを検知すると `/tmp/fbb/sys/class/remoteproc/remoteproc0/pid` を参照して `kill(pid, SIGUSR1)` を発行するため、FW 内に `/tmp` パスや `kill()` が露出することはありません。
2. **割り込み禁止／許可（`di` / `ei`）のペンディング保持再現**:
   - [bsp_sim_irq.c](bsp_sim_irq.c) は `mcore_bsp_di()` / `mcore_bsp_ei()` を `sigprocmask(SIG_BLOCK / SIG_UNBLOCK, &g_irq_mask, NULL)` で実装しています（ネスト対応カウンタ `g_irq_nest` 付き）。
   - `FRCC` の `GetFreeRunCounter()` が `(_di)()` 〜 `(_ei)()` でクリティカルセクションに入っている間にタイマー割り込みが到着しても、Linux カーネルが `SIGUSR1` をペンディング保持し、`(_ei)()` によるブロック解除直後に ISR を発火させるため、実機 ARM NVIC / GIC と等価な振る舞いを再現できます。

### 1.2 Zynq 実機（Cortex-R5 / Cortex-M）への移植方針
- [mcore_sfs.c](mcore_sfs.c) は 1 行も変更せず、リンク対象を `bsp_sim_irq.c` から実機用 BSP（例: `bsp_zynq_r5.c`）へ差し替えます。
  - `mcore_bsp_register_timer_isr`: `XScuGic_Connect()` と IRQ ハンドラ登録
  - `mcore_bsp_di` / `mcore_bsp_ei`: `__disable_irq()` / `__enable_irq()`（または `Xil_ExceptionDisable()` / `Enable()`）
  - `mcore_bsp_idle_hint`: `__WFI()`

---

## 2. 競合・安全性対策の詳細

### 2.1 `TIMER_INT_ACK` の Write-1-to-Clear (W1C) と SHM 同期順序
[vfpga_top.v](vfpga_top.v) では `TIMER_INT_ACK`（`0x4000000C`）の Read 応答を常に `32'h0` としています。`sim_main.cpp` のメインループは以下の順序で動作するため、M コアの ACK 書き込みが Read 同期に先回りして消されることはありません：

1. **起動直後ガード**: `sim_main.cpp` は初期状態で `old_shm` を `0xFF`、共有メモリを `0x00` で開始し、第 1 サイクルで全レジスタへ `0` を書き込みます。`vfpga_top.v` は `TIMER_CTRL[0] && (TIMER_PERIOD != 32'd0)` を条件にしているため、初期設定中の意図しない割り込み発火を防げます。
2. **毎サイクル Write → Read 順序**:
   - まず **Write-to-RTL**（`if (shm[off] != old_shm[off])`）が M コアの `1` 書き込みを検知し、`w_en=1` で RTL へ渡して `TIMER_STATUS[0] <= 0` にクリア、`old_shm[off] = 1` に更新します。
   - 続いて **Read-from-RTL**（`if (top->r_data != old_shm[off])`）が `0` を読み出し、`shm[off] = 0`・`old_shm[off] = 0` へ自動戻しします。
   - これにより次の割り込みでも M コアが同じ `REG_TIMER_INT_ACK = 1U;` を書くだけで確実に差分検知されます。
3. **単一 `always` ブロック設計**: `TIMER_CTRL`・`TIMER_PERIOD`・`TIMER_STATUS`・`TIMER_HW_CNT`・`timer_div` を単一の `always @(posedge clk or negedge rst_n)` にまとめることで Verilator `-Wall` でのマルチドライバ警告を排除しています。

### 2.2 ISR の非同期シグナル安全性
`on_timer_isr()` 内では `gFreeRunCounter++`、`gFreeRunCounterMini++`、および MMIO マッピング済みアドレスへの単一 32bit volatile 書き込み（`REG_TIMER_INT_ACK = 1U`）のみを実行し、`printf` やミューテックスを伴う標準ライブラリ呼び出しを完全に排除しています。

### 2.3 A コア ↔ M コア間コマンドハンドシェイク (`send_cmd_sync`)
[main.c](main.c) の全コマンド送信は `send_cmd_sync()` に統一しています：
1. 送信前：`CMD == 0` になるまで待機（前回コマンド未処理のままの上書きを防止）。
2. 書込順：`DATA_IN` を先に書き込み、`__sync_synchronize()` メモリバリア後に `CMD` を書き込む（ARM 実機の弱いメモリオーダリングでも順序逆転を防止）。
3. 送信後：M コアの `task_cmd_poll()` が処理完了して `CMD = 0` に戻すまで待機。これにより Burst 4x 投入でもパケットを取りこぼしません。

---

## 3. HW/SW 境界分離の実装原理

- `vfpga_top.v` は `vfpga_timer@40000000`（`0x40000000`〜`0x40000010`）のみをデコードし、それ以外のアドレスの Read には `32'hdeadbeef` を返します。
- `sim_main.cpp` は `top->r_data == 0xdeadbeef` のアドレスでは共有メモリを上書きしない仕様であるため、[config.dts](config.dts) で別ノード `amp_shared_mem@40001000` を定義しても、**Verilog を一切書かずに純粋な A コア ↔ M コア共有メモリ（Zynq OCM / AXI BRAM 相当）** として機能します。
- さらに `libfpgashim.c` の `init_mcore_mapping()` は隣接する `/dev/uio0`（`0x40000000`）と `/dev/uio1`（`0x40001000`）を検知して間のガードページ生成を回避しつつ両方を `MAP_FIXED_NOREPLACE` で連続マッピングするため、M コアから物理アドレスそのままのポインタアクセスが透過的に成立します。

---

## 4. スケジューラ周回と Tracer 200ms 周期の整合

- `mcore_sfs.c` のメインループは `while (SFS_dispatch()) { mcore_bsp_idle_hint(); }` としており、1 周回（`CMD_POLL` → `SM_WORKER` → `TELEM_HB`）の末尾で 1 回だけ `usleep(1000)` を実行します。これによりコマンド応答遅延を約 1ms に抑えつつ CPU 100% 占有を防ぎます。
- Web ダッシュボード（`dashboard/server.js`）の `updateShm()` ポーリング間隔は **200ms** です。
  - **自動回帰テストモード**: `TIMER_PERIOD = 500` クロック（約 50〜100ms/tick）で数秒以内にテスト完走。
  - **Lab 対話モード (`VFPGA_INTERACTIVE=1`)**: `main.c` 起動時に `CMD_SET_TIMER_PERIOD` で `1500` クロック（約 150〜300ms/tick）へ変更し、`SM_WORKER` の 2 ティック待機（`FRCGapCheckStart(..., 2UL)`）が Tracer 上で **2〜3 サンプル間隔の階段波形（`4 -> 3 -> 2 -> 1 -> 0`）** として明瞭に記録されるようにしています。
