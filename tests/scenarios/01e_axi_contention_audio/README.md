# Scenario 01e: AXI バス競合 ＆ オーディオ可変メモリストール・カオス検証

## 1. 概要
本シナリオは、FPGA + SoC（Zynq / Pynq-Z2等）の実機環境において頻発する **「Verilator上では問題なく動くが、実機に持っていくとPS側DRAMのバス競合や可変レイテンシでストールし、0.5μsのデッドラインを割って音声が歪む」** という問題を、PC上で決定論的に再現・検証するPoCシナリオです。

## 2. アーキテクチャ
- **ハードウェア (`vfpga_top.v`)**:
  - タイトな 4 サンプル FIFO を備えたオーディオ DMA 回路。
  - 4 サイクル周期（8MHz 動作時 0.5μs 相当）で 1 サンプルを消費。
  - AXI4 Master Read Channel (`m_axi_ar*`, `m_axi_r*`) を経由して DRAM からサンプルを逐次先読み供給。
- **カオス・エンジン (`AxiStallEngine`)**:
  - 通常実行（遅延ゼロ/理想環境）: 1 サイクルで即座に応答し、FIFO アンダーランは 0 回。
  - カオス実行 (`--chaos --chaos-targets=axi_stall`): 確率的に 8〜32 サイクルの突発的ストールを注入。FIFO が枯渇し、アンダーラン（音飛び）が確実に発生・検出される。

## 3. 実行方法

### 通常実行 (理想環境ベースライン検証: 0 underrun, 100% PASS)
```bash
fbb test 01e
```

### カオス実行 (実機特有の AXI バス競合・メモリストール再現検証: underruns > 0, 100% PASS)
```bash
fbb test 01e --chaos --chaos-targets=axi_stall --chaos-rate=0.20 --stall-min=8 --stall-max=32 --seed=12345
```

