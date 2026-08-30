# Scenario 18: Rust RTIC ハードリアルタイム制御 - 詳細設計 & アーキテクチャ解説

本ドキュメントは、ハードリアルタイム並行処理フレームワーク **RTIC (Real-Time Interrupt-driven Concurrency)**、デッドロックフリーな優先度継承プロトコル (SRP)、および仮想割り込みシグナルディスパッチの詳細仕様書です。

---

## 1. RTIC 割り込み駆動アーキテクチャ

```mermaid
graph TD
    subgraph "A-Core (Linux / C)"
        App["main.c (IPI Generator)"]
    end

    subgraph "Interrupt & Shared Memory Layer"
        VFPGA["Virtual Registers (SHM)"]
        Sig["POSIX SIGUSR1 (Virtual IRQ)"]
    end

    subgraph "M-Core (Rust RTIC)"
        Dispatcher["RTIC Interrupt Dispatcher"]
        PriorityTask["#[task(priority = 2, binds = SIGUSR1)]\nfn on_interrupt_request()"]
    end

    App -->|"MMIO Write & fbb_ipi_notify"| VFPGA & Sig
    Sig --> Dispatcher
    Dispatcher -->|"Priority Preemption"| PriorityTask
    PriorityTask -->|"Write Result"| VFPGA
```

---

## 2. RTIC (SRP) によるデッドロックフリー保証

RTIC は **Stack Resource Policy (SRP)** に基づいて共有リソースのアクセス優先度を制御するため、ミューテックス（Mutex）によるデッドロック（相互待ち）が数学的に発生しないことが保証されています。
