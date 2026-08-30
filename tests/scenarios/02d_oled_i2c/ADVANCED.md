# Scenario 02d: 仮想 I2C OLED ディスプレイ (SSD1306) - 詳細設計 & アーキテクチャ解説

本ドキュメントは、SSD1306 128x64 モノクロ OLED コントローラの仮想化、GDDRAM フレームバッファ展開、および WebSocket によるブラウザ描画同期の詳細仕様書です。

---

## 1. ディスプレイパイプラインアーキテクチャ

```mermaid
sequenceDiagram
    autonumber
    participant App as FW App (main.cpp)
    participant Shim as C-Shim (libfpgashim.so)
    participant Daemon as OLED Daemon (fbb_i2c_oled)
    participant SHM as Shared Memory (/dev/shm)
    participant Server as Dashboard Server (Node.js)
    participant UI as React UI (Canvas)

    App->>Shim: ioctl(/dev/i2c-0, I2C_RDWR) [GDDRAM Data]
    Shim->>Daemon: UNIX Domain Socket 転送
    Daemon->>SHM: 128x64 1-bit フレームバッファ書き込み (1024 Bytes)
    SHM-->>Server: フレーム更新検知
    Server-->>UI: WebSocket ブロードキャスト
    UI-->>UI: HTML5 Canvas リアルタイム描画 (Phosphor Glow)
```

---

## 2. SSD1306 コマンド & GDDRAM メモリ構造

SSD1306 は、128x64 ピクセルを 8 つの「Page（Page 0 〜 Page 7）」に分割して管理します。各 Page は 128 列 $\times$ 8 行（1バイト = 縦8ピクセル）で構成されます。

| Page | 行範囲 (Y座標) | バッファサイズ |
| :--- | :--- | :--- |
| Page 0 | Y: 0 〜 7 | 128 Bytes |
| Page 1 | Y: 8 〜 15 | 128 Bytes |
| ... | ... | ... |
| Page 7 | Y: 56 〜 63 | 128 Bytes |
| **合計** | **全 64 行** | **1024 Bytes (1 KB)** |
