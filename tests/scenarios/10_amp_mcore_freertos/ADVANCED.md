# Scenario 10: FreeRTOS による M コアマルチタスク制御 - 詳細設計 & アーキテクチャ解説

本ドキュメントは、FreeRTOS POSIX Port、マルチタスク間通信（`xQueue`）、優先度ベースプリエンプション、および Aコア（Linux）との共有レジスタコマンドプロトコルの詳細仕様書です。

---

## 1. FreeRTOS タスク間連携アーキテクチャ

```mermaid
graph TD
    subgraph "A-Core (Linux)"
        App["main.c (Client App)"]
    end

    subgraph "Shared MMIO Registers"
        CMD["0x10: REG_CMD"]
        DIN["0x14: REG_DATA_IN"]
        DOUT["0x18: REG_DATA_OUT"]
        STAT["0x1C: REG_STATUS"]
    end

    subgraph "M-Core (FreeRTOS Firmware)"
        T1["vTaskFPGAController (Priority: 1 / Polling)"]
        Queue["FreeRTOS xQueue (Item Size: 4B)"]
        T2["vTaskDataProcessor (Priority: 2 / Worker)"]
    end

    App -->|"Write Data & CMD=1"| DIN & CMD
    CMD & DIN -->|"Read CMD"| T1
    T1 -->|"ACK CMD=0 & Push Queue"| Queue
    Queue -->|"Pop Item"| T2
    T2 -->|"Calculate * 2"| T2
    T2 -->|"Write Result & STATUS=1"| DOUT & STAT
    STAT & DOUT -->|"Read Result"| App
```

---

## 2. FreeRTOS タスク設計と優先度

1. **`vTaskFPGAController` (優先度 1 / 低)**:
   - 仮想レジスタ `REG_CMD` を定期監視。
   - 要求を検知したら `REG_CMD` を 0 にクリア（ハンドシェイクACK）し、データを `xQueue` にプッシュ。
2. **`vTaskDataProcessor` (優先度 2 / 高)**:
   - `xQueueReceive()` でブロッキング待機。
   - キュー着弾時にプリエンプト起動し、データ加工（2倍演算）を実行して `REG_DATA_OUT` と `REG_STATUS` を更新。
