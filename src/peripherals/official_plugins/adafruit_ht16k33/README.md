# Adafruit 0.56" 4-Digit 7-Segment HT16K33 I2C Display Plugin

本プラグインは、業界で広く普及している **Adafruit 0.56" 4桁7セグメントLEDモジュール (Holtek HT16K33 I2C コントローラ内蔵 Backpack)** の PPA 2.0 仮想ペリフェラルエミュレータおよび 1:1 ベクター基板アセットです。

---

## 1. 製品概要および公式データシート

* **製品名**: Adafruit 0.56" 4-Digit 7-Segment Display w/ HT16K33 Backpack
* **メーカー**: Adafruit / Holtek
* **接続インターフェース**: I2C バス (デフォルトスレーブアドレス: `0x70`)
* **コントローラ IC**: Holtek HT16K33 (16*8 RAM Mapping LED Driver)
* **公式リンク**:
  * **[Adafruit 製品ページ (Product ID: 879)](https://www.adafruit.com/product/879)**
  * **[Adafruit LED Backpack Downloads & Datasheets Page](https://learn.adafruit.com/adafruit-led-backpack/downloads)**
  * **[Holtek HT16K33 Datasheet (PDF)](https://cdn-shop.adafruit.com/datasheets/ht16K33v110.pdf)**

---

## 2. デバイスツリー (`config.dts`) への組み込み方法

DTS ファイル内で `ht16k33.dtsi` を include するか、以下のように I2C ノードへ定義を追加します。

```dts
&i2c1 {
    seg7_0: seg7@70 {
        compatible = "adafruit,ht16k33-red", "holtek,ht16k33";
        reg = <0x70>;
    };
};
```

---

## 3. モデル型番 (`compatible`) による発光カラー指定

DTS の `compatible` 属性の記述を変更するだけで、実在する Adafruit 各製品モデル（発光カラー）と Web UI の発光ネオンカラーが自動的に連動して切り替わります：

| 発光カラー | `compatible` 属性の指定例 | 対応 Adafruit 製品 ID |
| :--- | :--- | :---: |
| **Red (赤色 - デフォルト)** | `compatible = "adafruit,ht16k33-red", "holtek,ht16k33";` | **Product ID: 879** |
| **Green (緑色)** | `compatible = "adafruit,ht16k33-green", "holtek,ht16k33";` | **Product ID: 880** |
| **Yellow (黄色)** | `compatible = "adafruit,ht16k33-yellow", "holtek,ht16k33";` | **Product ID: 881** |
| **Blue (青色)** | `compatible = "adafruit,ht16k33-blue", "holtek,ht16k33";` | **Product ID: 882** |
| **White (白色)** | `compatible = "adafruit,ht16k33-white", "holtek,ht16k33";` | **Product ID: 1002** |

---

## 4. 構成ファイル

- **`fbb-plugin.json`**: プラグイン定義マニフェスト（`compatible` マッピング、`7seg_display` コントロール宣言）。
- **`board.svg`**: 1:1 ピクセルパーフェクト基板ベクターアセット（黒色PCB、表示窓枠、ピンアサイン `CLK`, `DAT`, `VCC`, `GND`）。
- **`ht16k33.cpp`**: C++ エミュレータ（`I2cSlave` を継承。オシレータ `0x21`、点滅 `0x81`~`0x87`、輝度 `0xE0`~`0xEF`、表示RAM `0x00`~`0x0F` エミュレーション）。
- **`ht16k33.dtsi`**: 標準デバイスツリーインクルードファイル。
