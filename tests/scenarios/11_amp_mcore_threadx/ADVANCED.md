# Scenario 11: Eclipse ThreadX による M コアマルチタスク制御 - 詳細設計 & アーキテクチャ解説

本ドキュメントは、Eclipse ThreadX (Azure RTOS) Linux Port、スレッド間メッセージキュー (`TX_QUEUE`)、優先度継承、および Aコア（Linux）との共有レジスタコマンドプロトコルの詳細仕様書です。

---

## 1. ThreadX スレッド間連携アーキテクチャ

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

    subgraph "M-Core (ThreadX Firmware)"
        T1["thread_controller (Priority: 1 / Polling)"]
        Queue["ThreadX TX_QUEUE (Message Queue)"]
        T2["thread_processor (Priority: 2 / Worker)"]
    end

    App -->|"Write Data & CMD=1"| DIN & CMD
    CMD & DIN -->|"Read CMD"| T1
    T1 -->|"ACK CMD=0 & tx_queue_send"| Queue
    Queue -->|"tx_queue_receive"| T2
    T2 -->|"Calculate * 2"| T2
    T2 -->|"Write Result & STATUS=1"| DOUT & STAT
    STAT & DOUT -->|"Read Result"| App
```

---

## 2. ThreadX API と設計仕様

1. **`tx_kernel_enter()`**: ThreadX カーネル初期化およびエントリポイント。
2. **`tx_application_define()`**: スレッド（`tx_thread_create`）およびキュー（`tx_queue_create`）の静的リソース割り当て。
3. **`tx_queue_send` / `tx_queue_receive`**: 高速なスレッドセーフ・メッセージ受け渡し。
