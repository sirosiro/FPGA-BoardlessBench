# 01b_uio_irq_interrupt: ハードウェアからの合図「割り込み（IRQ）」

前回の [01_standard_uio](../01_standard_uio/README.md) では、FPGAのカウンターが進むのを `sleep(1)` で適当に待っていました。
しかし実際の製品では、「いつ処理が終わるか分からないのに `sleep` で待つ」のはCPU時間の無駄です。

このシナリオでは、ハードウェアから「終わったよ！」とCPUに合図を送る **「割り込み（Interrupt / IRQ）」** の基本を体験します。

---

## このシナリオのゴール
**「C言語プログラムを無駄に `sleep` させず、FPGAから割り込み通知が届くまでスマートに待機（ブロック）する」**

---

## 直感イメージ：CPUとFPGAのやり取り
割り込みは、**「ハードウェアからCPUへの電話」** のような仕組みです。

```mermaid
flowchart LR
    subgraph CPU ["CPU (C言語: main.c)"]
        C_Wait["read() で待機\n(CPU負荷 0% で休止)"]
        C_Handle["起きて処理再開！\n(INT_ACK を書く)"]
    end

    subgraph Hardware ["FPGA (vfpga_top.v)"]
        Timer["ハードウェアタイマー\n(2000クロック数える)"]
        IRQ_Line["割り込み信号 (irq_out = 1)"]
    end

    Timer -->|"時間になった！"| IRQ_Line
    IRQ_Line -->|"電話を鳴らす (割り込み)"| C_Wait
    C_Wait --> C_Handle
    C_Handle -->|"受話器を置く (リセット)"| Timer
```

---

## 3つの基本ステップ（コードの読み方）

[main.c](main.c) で行っていることは、以下の3ステップです。

1. **タイマーを動かし、割り込み待ち受けをONにする**
   - `regs[0] = 0x1;` でタイマーをスタートします。
   - `write(fd, &unmask, 4);` でLinuxに「このデバイスからの割り込み通知を受け取る準備ができたよ」と伝えます。
2. **割り込みが来るまで眠って待つ (`read`)**
   - `read(fd, &irq_count, sizeof(irq_count));` を呼ぶと、FPGAから合図が届くまでプログラムが一時停止（休止）します。CPUを1%も無駄遣いしません。
3. **合図を受け取ったら了解の返事をする (`INT_ACK`)**
   - FPGAが割り込みを発生させると、`read` がパッと解除されて処理が再開します。
   - `regs[REG_INT_ACK_OFFSET / 4] = 0x1;` と書いて、「通知を受け取ったから合図（フラグ）を消していいよ」とFPGAに伝えます。

---

## 1. まずは動かしてみよう！

ターミナルで以下のコマンドを実行します。

```bash
./run.sh
```

**期待される出力例：**
```text
=== Scenario 01b: UIO Asynchronous Interrupt (IRQ) Test ===
[FW Main] Successfully mapped UIO MMIO registers.
[FW Main] Hardware Timer Enabled (CTRL = 1).
[FW Main] UIO Interrupt Unmasked. Waiting for IRQ via read()...
[FW IRQ Handler] Interrupt Received! Accumulated IRQs: 1
[FW IRQ Handler] STATUS reg: 0x00000001, CNT reg: 1
[FW IRQ Handler] INT_ACK written. IRQ successfully handled.
=== Scenario 01b Test Result: SUCCESS ===
```
`read()` の行で待機し、FPGAのタイマーが満了した瞬間に即座に応答していることが確認できます！

---

## 2. ちょこっと改造チャレンジ！

理解を深めるために、ハードウェア（Verilog）とソフトウェア（C言語）のコードをちょこっと変えて動かしてみましょう。

- **チャレンジ1（ハードウェア編: Verilog）:**  
  [vfpga_top.v](vfpga_top.v#L58) の 58行目を見てみてください。
  ```verilog
  if (timer_divider >= 16'd2000) begin
  ```
  この `2000` を `200`（10倍速）や `20000`（10倍遅い）に書き換えて、もう一度 `./run.sh` を実行してみてください。  
  割り込みが届くまでの時間が変化することが実感できます！

- **チャレンジ2（ソフトウェア編: C言語）:**  
  [main.c](main.c#L46-L75) では、現在 1 回だけ割り込みを受け取ってすぐに終了しています。  
  これを、**「定期タイマーのように 5 回連続で割り込みを受け取るループ」** に改造してみましょう！
  ```c
  for (int i = 0; i < 5; i++) {
      // ① 次の割り込みを受け取るため、再アンマスクする
      uint32_t unmask = 1;
      write(fd, &unmask, sizeof(unmask));

      // ② 割り込みが届くまで眠って待つ
      uint32_t irq_count = 0;
      read(fd, &irq_count, sizeof(irq_count));

      // ③ カウンターと累計割り込み回数を表示
      uint32_t cnt = regs[REG_CNT_OFFSET / 4];
      printf("[FW IRQ Handler] #%d 回目の割り込み受信! (CNT: %u, 累計IRQ: %u)\n",
             i + 1, cnt, irq_count);

      // ④ ACK を書いて合図をクリア（受話器を置く）
      regs[REG_INT_ACK_OFFSET / 4] = 0x1;
  }
  ```
  書き換えて `./run.sh` を実行すると、一定周期でテンポよく `#1` 〜 `#5` の割り込み通知が届く様子が確認できます！  
  ハードウェア編（Verilog の `200` や `20000`）と組み合わせると、割り込みが届くテンポが劇的に変化するのを肌で体感できます。

  > **発展クイズ：もし ④ の `INT_ACK` を書き忘れたらどうなる？**  
  > 試しに `regs[REG_INT_ACK_OFFSET / 4] = 0x1;` をコメントアウトして実行してみてください。  
  > ハードウェア側が「電話を鳴らしっぱなし（STATUS[0]=1）」のままになり、次の割り込みが正常に検出できなくなるはずです。「割り込みを受け取ったら必ずACKでクリアする」という組み込みの鉄則が実感できます。

---

## 次のステップへ
これで「レジスタの読み書き」に加えて「ハードウェアからの割り込み通知」を受け取れるようになりました！

- **次のシナリオ [06_gpio](../06_gpio/README.md)**:  
  次は、LEDの点灯やスイッチの入力など、1ビット単位で信号をやり取りする **「GPIO（汎用入出力）」** に進みましょう。

---

## さらに詳しく知りたい方へ
C-Shimの内部IPC構造（eventfdやUNIX Socketの挙動）や実機Linuxへの移植手順などの詳細な技術仕様は、**[ADVANCED.md](ADVANCED.md)** を参照してください。
