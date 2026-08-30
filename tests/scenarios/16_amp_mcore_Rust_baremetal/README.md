# 16_amp_mcore_Rust_baremetal: 安全な次世代言語「組み込みRust」

組み込み開発の世界では長年C言語が使われてきましたが、近年「メモリ破壊バグやヌルポインタ参照をコンパイル時に100%防いでくれる」モダンなプログラミング言語 **「Rust（ラスト）」** が急速に普及しています。

このシナリオでは、マイコン（Mコア）側のファームウェアをC言語ではなく **Rust** で記述し、Linux（Aコア）と協調動作させる方法を体験します！

---

## このシナリオのゴール
**「マイコン側のファームウェアをRustで実装し、PAC（周辺機器アクセスクレート）を使って安全にレジスタを読み書きする」**

---

## 直感イメージ：CPUとFPGAのやり取り
Rustの強力な型システム（PAC）により、レジスタのアドレス計算ミスをコンパイル時に完全に防ぎながら、C言語のLinuxアプリと会話します。

```mermaid
flowchart LR
    subgraph A_Core ["Aコア (Linux: main.c)"]
        LinuxApp["C言語アプリケーション"]
    end

    subgraph Memory ["仮想FPGAレジスタ"]
        Regs["CMD, STATUS, DATA_IN, DATA_OUT"]
    end

    subgraph M_Core ["Mコア (Rust: mcore.rs)"]
        PAC["PAC (型安全なレジスタ操作)"]
        RustApp["Rustファームウェア\n(メモリ破壊バグ 0%)"]
    end

    LinuxApp -->|"データを書き込み"| Regs
    Regs <--> PAC
    PAC <--> RustApp
    Regs -->|"計算結果を読み出し"| LinuxApp
```

---

## 3つの基本ステップ（コードの読み方）

[mcore.rs](mcore.rs) で行っていることは、以下の3ステップです。

1. **周辺機器（レジスタ）のインスタンスを取得する**
   - `let dp = fbb_pac::Peripherals::take().unwrap();`
   - Rustのシングルトン機構により、同じレジスタを複数箇所から不正に多重オープンするミスを防ぎます。
2. **型安全にレジスタを読み書きする**
   - `dp.vfpga_reg.cmd.read()` や `dp.vfpga_reg.data_out.write(result)` のように、構造体のメソッドとして安全にアクセスします。
3. **Linuxからのリクエストを処理して返す**
   - 受信した `data_in` を2倍に計算し、`data_out` に書き込んで `status` を更新します。

---

## 1. まずは動かしてみよう！

ターミナルで以下のコマンドを実行します（Rustコンパイラ `rustc` で自動ビルドされます）。

```bash
./run.sh
```

**期待される出力例：**
```text
=== Scenario 16: Embedded Rust on M-Core Test Start ===
[A-Core] Booting Rust M-Core firmware...
[M-Core Rust] Rust Firmware Started! Safe MMIO access via PAC initialized.
[A-Core] Sending Task Request: DATA_IN = 0x00000100...
[M-Core Rust] Processed Request in Rust: Result = 0x00000200
[A-Core] Received Result: DATA_OUT = 0x00000200
[A-Core] SUCCESS: Embedded Rust M-Core coordination verified!
=== Scenario 16 Test Result: SUCCESS ===
```

---

## 2. ちょこっと改造チャレンジ！

理解を深めるために、Rustコードの計算式を変えてみましょう。

- **実験:** [mcore.rs](mcore.rs) の計算ロジック（`data * 2`）を `data + 0x5555` などに変更して `./run.sh` を実行してみてください。  
  Rustでビルドされたファームウェアが即座に再コンパイルされ、新しい計算結果を返す様子が確認できます！

---

## 次のステップへ
これで「組み込みRustの基本（PACとベアメタル）」が身につきました！

- **次のシナリオ [17_amp_mcore_Rust_embassy](../17_amp_mcore_Rust_embassy/README.md)**:  
  次は、Rustの最新機能である非同期処理（async/await）を活用したモダンフレームワーク**「Embassy」**に進みましょう。

---

## さらに詳しく知りたい方へ
リンク時多態（Link-time Polymorphism）による実機透過性の実現方法や `MAP_FIXED_NOREPLACE` エミュレーションの詳細は、**[ADVANCED.md](ADVANCED.md)** を参照してください。
