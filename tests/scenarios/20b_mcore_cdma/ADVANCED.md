# Scenario 20b: AMP M-Core AXI CDMA オフロード - 詳細設計 & アーキテクチャ解説

本ドキュメントは、Mコア（リアルタイムプロセッサ）主導の AXI CDMA 直接制御、Aコア Linux との連携、およびアライメント/境界エラー検知機構の詳細仕様書です。

---

## 1. Mコア CDMA オフロードアーキテクチャ

```mermaid
flowchart TD
    subgraph A_Core["A-Core (Linux)"]
        Host["main.c (remoteproc 管理)"]
    end

    subgraph M_Core["M-Core (Real-time Firmware)"]
        FW["mcore_cdma.c (Direct CDMA Controller)"]
    end

    subgraph Hardware["FPGA / Shared Memory"]
        CDMA["AXI CDMA Controller (0x40002000)"]
        SRC["Source Memory (0x40000000)"]
        DST["Destination Memory (0x40003000)"]
    end

    Host -->|"Boot M-Core"| FW
    FW -->|"Write BTT / Direct Control"| CDMA
    CDMA -->|"Hardware DMA Copy"| SRC
    SRC --> DST
```

---

## 2. 特徴と利点

- **Linux CPU負荷ゼロ**: DMA のセットアップ・転送待ちをすべて M コアが担うため、Linux（Aコア）は UI やネットワーク処理に専念可能。
- **ミリ秒以下の超低遅延起動**: Linux のスケジューラやコンテキストスイッチを挟まず、ハードウェア割り込みに応答して即座に DMA をキック可能。
