# Scenario 15: OpenAMP / RPMsg with FreeRTOS - 詳細設計 & アーキテクチャ解説

本ドキュメントは、FreeRTOS タスク環境下で動作する OpenAMP / RPMsg プロトコルスタック、セマフォによるイベント待機、およびスレッドセーフなコア間メッセージングの詳細仕様書です。

---

## 1. FreeRTOS + OpenAMP 協調アーキテクチャ

```mermaid
graph TD
    subgraph "A-Core (Linux)"
        App["main.c (/dev/rpmsg0)"]
    end

    subgraph "Shared Memory (0x3ee00000)"
        Vring["VirtIO vring + Buffer Pool"]
    end

    subgraph "M-Core (FreeRTOS Tasks)"
        RPTask["prvOpenAMPTask (RPMsg Handler)"]
        Sem["Binary Semaphore (IPI Signal)"]
    end

    App <--> Vring
    Vring <--> RPTask
    Sem -->|"Wakeup Task"| RPTask
```

---

## 2. FreeRTOS タスクと RPMsg コールバックの連携

FreeRTOS 環境下では、シグナルハンドラ（またはハードウェア ISR）からセマフォ（`xSemaphoreGiveFromISR`）を解放し、`prvOpenAMPTask` が起床して `virtqueue_notification` を安全にディスパッチする構造を採用しています。
