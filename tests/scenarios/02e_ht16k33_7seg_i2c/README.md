# シナリオ 02e: 多重 I2C ペリフェラル制御 (SSD1306 OLED & Adafruit HT16K33 7セグメントLED)

本シナリオでは、実世界の IoT 組込み端末（スマートステーション）を模し、同一 I2C バス（`/dev/i2c-0`）上に **モノクロ OLED ディスプレイ (SSD1306)** と **4桁 7セグメントLEDモジュール (Adafruit HT16K33 Backpack)** の 2 つのペリフェラルを混在マウントして並列制御・動作検証します。

![FPGA-BoardlessBench (F-BB) AroundView Dashboard](assets/dashboard.gif)

---

## 1. 使用するデバイス仕様およびデータシートへのリンク

### ① Adafruit 0.56" 4-Digit 7-Segment Display w/ HT16K33 Backpack
* **デバイス名**: Adafruit 0.56" 4-Digit 7-Segment LED (Holtek HT16K33 コントローラ内蔵)
* **接続方法**: I2C バス (デフォルトスレーブアドレス: `0x70`)
* **データシートおよび公式リンク**:
  * **[Adafruit 0.56" 4-Digit 7-Segment Display 製品ページ (Product ID: 879)](https://www.adafruit.com/product/879)**
  * **[Adafruit LED Backpack Downloads & Datasheets Page](https://learn.adafruit.com/adafruit-led-backpack/downloads)**
  * **[Holtek HT16K33 Datasheet (PDF)](https://cdn-shop.adafruit.com/datasheets/ht16K33v110.pdf)**

### ② Solomon Systech SSD1306
* **デバイス名**: Solomon Systech SSD1306 (128x64ドット モノクロOLEDコントローラ)
* **接続方法**: I2C バス (デフォルトスレーブアドレス: `0x3C`)
* **データシートへのリンク**:
  * **[Solomon Systech SSD1306 Datasheet (Adafruit PDF)](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf)**

---

## 2. システム構成と複数ペリフェラルの連動

本シナリオを実行すると、以下のコンポーネントが協調して動作します：

1. **テストアプリケーション (`test_bin` / `main.cpp`)**:
   単一の `/dev/i2c-0` を介して、アドレス `0x3C` (OLED) と `0x70` (7セグLED) へ交互に通信フレームを送信。
2. **システムコールShim (`libfpgashim.so`)**:
   アプリケーションからの I2C システムコール（`ioctl(I2C_RDWR)`）をフックし、宛先スレーブアドレスに応じてそれぞれのペリフェラルデーモンへ動的にルーティング。
3. **独立ペリフェラルデーモン**:
   - **`fbb_i2c_oled`**: SSD1306 描画データをパースし `/dev/shm/fbb_display_0` へ同期。
   - **`fbb_i2c_ht16k33`**: HT16K33 コマンド・表示RAMをパースし `/dev/shm/fbb_display_7seg_0` へ同期。
4. **Webダッシュボード (React UI)**:
   Dockview レイアウト上で **`SSD1306 OLED Display`** ペインと **`Adafruit 4-Digit 7-Segment LED`** ペインの 2 つを独立マウントし、タイマーカウントアップおよび診断 Hex コード（`dEAd` / `bEEF`）の点滅をリアルタイム同期表示。

---

## 3. 実行方法

プロジェクトルート、またはこのディレクトリ配下で起動スクリプトを実行します。

```bash
./run.sh
```

ブラウザで `http://localhost:8080` を開くと、OLED ペインと 7セグLED ペインが並列マウントされ、時刻タイマーおよび 16進数診断コードの明滅アニメーションを確認できます。

---

## 4. 実機（Zynq / Linux）透過性

本シナリオの `main.cpp` は、Linux 標準の `ioctl(I2C_RDWR)` を使用して記述されているため、実機 Zynq や Raspberry Pi などの Linux ボード上でも 1 行のコード変更もなくそのままコンパイル・動作します。
