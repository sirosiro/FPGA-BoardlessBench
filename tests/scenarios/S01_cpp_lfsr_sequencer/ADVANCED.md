# Scenario S01: C++ LFSR シーケンサー - 詳細設計 & アーキテクチャ解説

本ドキュメントは、C++ オブジェクト指向 HAL 設計、カスタム Verilog LFSR (線形帰還シフトレジスタ) パターンエンジン、およびシステム統合ショーケースの詳細仕様書です。

---

## 1. システム統合アーキテクチャ

```mermaid
graph TD
    subgraph "Application Layer (C++)"
        App["main.cpp (Interactive Shell)"]
        RegClass["Register Model Hierarchy (OOA/OOD)"]
    end

    subgraph "Interception Layer"
        Shim["libfpgashim.so (C-Shim)"]
    end

    subgraph "Hardware Layer (RTL / Shared Memory)"
        subgraph "Standard IP"
            UART["UART Lite (/dev/ttyPS1)"]
            GPIO["AXI GPIO (0x41200000)"]
        end
        subgraph "Custom Verilog IP"
            Engine["pattern_engine.v (FSM)"]
            LFSR["LFSR PRNG Core (Galois LFSR)"]
        end
    end

    subgraph "Observation Layer"
        Dash["Web Dashboard (Port 8080)"]
        Wave["GTKWave (vfpga.vcd)"]
    end

    App --> RegClass
    RegClass -->|"MMIO"| Shim
    Shim --> Hardware
    Hardware <--> Dash
    Hardware <--> Wave
```

---

## 2. C++ ファームウェアクラス設計

- **`MemoryMappedDevice`**: 物理 MMIO アクセスを抽象化した基底クラス。
- **`UioDevice`**: `/dev/uioX` の `open` / `mmap` / `munmap` を RAII パターンで安全に管理するカプセル化クラス。
- **`LfsrEngine`**: パターン生成・ステートマシン制御を担当する高水準ドライバクラス。
