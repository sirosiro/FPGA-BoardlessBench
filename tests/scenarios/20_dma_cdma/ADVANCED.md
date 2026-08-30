# Scenario 20: Zynq AXI CDMA - 詳細設計 & アーキテクチャ解説

本ドキュメントは、Xilinx AXI CDMA IP (`xlnx,axi-cdma-1.00.a`) のエミュレーション、アライメント検査 (`DMADecErr`)、バッファアンダーラン/オーバーラン検出、および実機ポーティングに関する詳細仕様書です。

---

## 1. AXI CDMA 転送パイプラインアーキテクチャ

```mermaid
flowchart TD
    FW["Firmware Application (main.c)"]
    
    FW -->|"1. open() / mmap()"| SHIM["libfpgashim.so (C-Shim)"]
    FW -->|"2. Write BTT (Bytes to Transfer 0x28)"| SHIM
    
    subgraph SHIM_PROC["C-Shim Interception & Emulation Engine"]
        SHIM --> HOOK["Hook Write to BTT"]
        HOOK --> TRANS["Translate Physical Address → SHM Offset"]
        TRANS --> CHK{"Validate 32-bit Alignment & Boundaries"}
    end
    
    CHK -->|"Valid"| COPY["Fast Synchronous memcpy"]
    CHK -->|"Unaligned"| FAIL["Reject & Set CDMASR (Bit 6: DMADecErr)"]
    
    COPY --> SHM["POSIX Shared Memory (SHM)\n- SRC (0x40000000)\n- DST (0x40003000)\n- CDMA Regs (0x40002000)"]
    FAIL --> SHM
    
    SHM --> DASH["Web Dashboard (Register Monitor / Live Telemetry)"]
```

---

## 2. AXI CDMA レジスタマップ (`config.dts`)

```text
ベースアドレス : 0x40002000 (サイズ: 4KB)
デバイスノード : /dev/uio0
```

| オフセット | レジスタ名 | 属性 | 機能説明 |
| :--- | :--- | :--- | :--- |
| `0x00` | `CDMACR` | R/W | CDMA 制御レジスタ (`bit 2`: リセット) |
| `0x04` | `CDMASR` | R | CDMA ステータスレジスタ (`bit 1`: Idle, `bit 6`: DMADecErr) |
| `0x18` | `SA` | R/W | 転送元ソース物理アドレス (Source Address: 32-bit) |
| `0x20` | `DA` | R/W | 転送先デスティネーション物理アドレス (Destination Address: 32-bit) |
| `0x28` | `BTT` | R/W | 転送バイト数 (Bytes to Transfer: 書き込みで転送トリガー) |

---

## 3. エラー検出ロジック

1. **DMADecErr (非アライメントエラー: Bit 6)**:
   `SA` または `DA` が 4 バイト境界（下位 2 ビットが非ゼロ）にない場合、転送を拒否して `CDMASR` の Bit 6 をセット。
2. **バッファオーバーラン (OVERRUN_ERR: Bit 5)**:
   転送先領域の許容境界（4KB）を超える転送が要求された場合、溢れたデータを破棄してエラーを通知。
