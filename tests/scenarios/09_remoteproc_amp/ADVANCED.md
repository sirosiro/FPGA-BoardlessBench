# Scenario 09: remoteproc による M コアライフサイクル制御 - 詳細設計 & アーキテクチャ解説

本ドキュメントは、Linux `remoteproc` フレームワーク（Sysfs インターフェース）、異種マルチコア（Aコア Linux + Mコア マイコン）のライフサイクル管理、および動的ファームウェア・ホットスワップ機構の詳細仕様書です。

---

## 1. remoteproc ライフサイクルアーキテクチャ

```mermaid
graph TD
    subgraph "A-Core (Linux / Host Space)"
        App["main.c (A-Core App)"]
    end

    subgraph "F-BB Interception & Daemon Layer"
        Shim["libfpgashim.so (Path Interceptor)"]
        Daemon["vlogic_controller.py (Process Manager)"]
        Sysfs["/sys/class/remoteproc/remoteproc0/\n(state, firmware, pid)"]
    end

    subgraph "M-Core (Co-Processor Firmware)"
        FW1["mcore_baremetal.elf (FW1)"]
        FW2["mcore_baremetal2.elf (FW2)"]
    end

    subgraph "Shared Memory & Hardware"
        SHM["/tmp/vfpga_reg (MMIO Registers)"]
    end

    App -->|"/sys/class/remoteproc/..."| Shim
    Shim --> Sysfs
    Daemon -->|Watches state| Sysfs
    Daemon -->|Fork/Exec/Kill| FW1
    Daemon -->|Fork/Exec/Kill| FW2
    
    FW1 & FW2 <--> SHM
    App <--> SHM
```

---

## 2. Linux 標準 remoteproc Sysfs 制御シーケンス

```bash
# 1. ロードするファームウェアバイナリ名を指定
echo "mcore_firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware

# 2. リモートコアの起動
echo "start" > /sys/class/remoteproc/remoteproc0/state

# 3. 稼働状態の確認 (running が返る)
cat /sys/class/remoteproc/remoteproc0/state

# 4. リモートコアの停止
echo "stop" > /sys/class/remoteproc/remoteproc0/state
```

---

## 3. 動的ホットスワップ時のレースコンディション防止設計

稼働中のファームウェア（FW1）を別ファームウェア（FW2）に差し替える際、以下の厳密なハンドシェイクを実行します：
1. `echo "stop" > state` を発行
2. `state` が `"offline"` に遷移するまでポーリング待機（プロセスの完全回収を保証）
3. `echo "fw2.elf" > firmware` を書き込み
4. `echo "start" > state` を発行して新ファームウェアを起動
