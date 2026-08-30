# Scenario 16: Rust によるベアメタル M コア - 詳細設計 & アーキテクチャ解説

本ドキュメントは、Embedded Rust、DTS 駆動の PAC (Peripheral Access Crate)、リンク時多態（Link-time Polymorphism）、および `MAP_FIXED_NOREPLACE` によるホスト MMIO エミュレーションの詳細仕様書です。

---

## 1. Rust PAC & リンク時多態アーキテクチャ

```mermaid
graph TD
    subgraph "Firmware Core (100% Target Independent)"
        Core["mcore.rs (Pure Logic)"]
    end

    subgraph "PAC Layer"
        PAC["fbb_pac.rs (Peripheral Access Crate)"]
    end

    subgraph "BSP Layer (Link-time Selection)"
        HostBSP["host_bsp.rs (F-BB Simulation BSP)"]
        TargetBSP["target_bsp.rs (Cortex-M Hardware BSP)"]
    end

    Core --> PAC
    Core -->|"extern C (delay_ms, etc.)"| HostBSP
    Core -.->|"extern C"| TargetBSP
```

---

## 2. PAC による型安全なレジスタアクセス

```rust
let dp = Peripherals::take().unwrap();
// 生ポインタではなく型安全な Register<T> でアクセス
dp.vfpga_reg.cmd.write(0x1);
let status = dp.vfpga_reg.status.read();
```
DTS から自動生成された `fbb_pac.rs` により、アドレスの直打ちやポインタ演算ミスをコンパイル時に完全に排除します。
