# ARCHITECTURE_MANIFEST: FPGA-BoardlessBench (F-BB)

これは、FPGA-BoardlessBench (F-BB)プロジェクト全体の運用・開発インフラに関わるポリシーである.

---

## Part 1: このマニフェストの取扱説明書 (Guide)

### 1. 目的 (Purpose)
本マニフェストは、Linux上でのFPGAエミュレーション環境構築における「北極星」として、開発者とAIが共有する普遍的な原則と設計判断を記録する。これにより、場当たり的な実装を防ぎ、長期的な保守性と実機との高い互換性を担保する。

### 2. 憲章の書き方 (Guidelines)
- **原則:** 「なぜ（Why）」を記述する。トレードオフの判断背景を明記すること。
- **具体性:** 抽象的な表現を避け、検証可能な目標や境界条件を定義する。
- **優先順位:** 常にこのマニフェストを実装コードよりも優先する。

### 3. リスクと対策 (Risks and Mitigations)
- **リスク:** 実機ドライバの複雑な挙動を再現しきれない。
- **対策:** 実機でのパケットログをキャプチャし、エミュレータ側で「リプレイ」できるテスト機構を設計に含める。

### 4. サブ・マニフェスト (Sub-Manifests)
- **[Scripts Generator](./scripts/ARCHITECTURE_MANIFEST.md)**: DTS から環境を自動生成するコア・ロジックの設計。
- **[Dashboard Interface](./dashboard/ARCHITECTURE_MANIFEST.md)**: 診断ダッシュボードと可視化レイヤーの設計。
- **[Test Scenarios](./tests/scenarios/ARCHITECTURE_MANIFEST.md)**: 各テストシナリオの共通原則、禁止事項、および個別シナリオの役割定義。
- **[i.MX HAL Scenario](./tests/scenarios/P01_frdmIMX/hal/ARCHITECTURE_MANIFEST.md)**: 実機とシミュレータ環境における UART/GPIO の差異を吸収し、コードの透過性を担保する i.MX HAL の設計。

---

## Part 2: マニフェスト本体 (Content)

### 1. 核となる原則 (Core Principles)
- **原則 1: 実機透過性の維持 (Hardware Transparency)**
  - **内容:** アプリケーション側のソースコードに「エミュレーション用の条件分岐」を一切持ち込まない。
  - **理由:** 実機環境とエミュレーション環境で同一のバイナリを動かすことで、環境依存のバグ混入を物理的に排除するため。
- **原則 2: 意図駆動エミュレーション (Intent-Driven Emulation)**
  - **内容:** RTLを100%完璧に再現することよりも、アプリケーションが期待する「レジスタの応答」や「プロトコルの振る舞い」を正しく返すことを優先する。
  - **理由:** FW開発のスピードを最大化するため。詳細なタイミング検証は[Verilator](./docs/architecture/AddInfo_verilator.md)等に責務を分離する。
- **原則 3: 単一の情報源 (Single Source of Truth) としてのDTS**
  - **内容:** ハードウェア定義（アドレス、バス構成等）は必ずDTSファイルのみに記述し、Shim（Cコード）、RTLスケルトン、シミュレーションドライバは全てDTSから自動生成する。「DTSを変更せずに手動でコードを書き換えて辻褄を合わせる行為」を固く禁ずる。
  - **理由:** ソフトウェア（Shim）とハードウェア（RTL）のインターフェースの不一致を構造的に排除し、実機構成との完全な同期を保証するため。
- **原則 4: ビルド責務の分離 (Separation of Build Responsibilities)**
  - **内容:** プロジェクトルートの `Makefile` は、FPGA-BoardlessBench (F-BB)自体の「コアコンポーネント（Shimや仮想ロジックエンジン本体）のコンパイル」のみに専念する。DTSからのコード自動生成や、シナリオ固有のFWコンパイル、テストの実行制御は `Makefile` に混ぜ込まず、`tests/run_tests.sh` 側に完全に委譲する。
  - **理由:** 共通基盤であるシミュレータのビルドと、各テストシナリオのビルド・実行サイクルを明確に分離することで、インフラ設定の肥大化・複雑化を防ぐため。
- **原則 5: フェイルファーストとクリーンログの義務 (Fail-Fast and Clean Logs)**
  - **内容:** テストランナーやビルドスクリプトは、CコンパイラやVerilatorでエラーが発生した時点で即座に実行を停止（`exit 1`）しなければならない。「ビルドに失敗しているのに、古いバイナリを使ってテストが無理やり成功してしまう（False Positive）」状態を許容しない。また、学習者の混乱を防ぐため、コンパイラのWarning（警告）も極力ゼロに保つ。
  - **理由:** エラーの隠蔽による誤った学習やデバッグの長期化を防ぎ、学習者が自身のコードの問題点に即座に気づける健全なフィードバックループを維持するため。
- **原則 6: シナリオの自律性と可搬性 (Scenario Autonomy & Portability)**
  - **内容:** 各テストシナリオは、単体で「ビルド (`Makefile`)」「実行 (`run.sh`)」「解説 (`README.md`)」を完結させなければならない。また、ビルド環境は特定のOSやツールに依存させず、標準的な環境変数を尊重する。
  - **理由:** 学習者が特定の課題に集中して取り組めるようにするとともに、FPGA-BoardlessBench (F-BB) で作成したソースコード一式を、そのまま PetaLinux 等の実機プロジェクトへ移行可能にするため。
- **原則 7: 学習者視点の徹底と内部ロジックの隠蔽 (Learner-Centric Purity)**
  - **内容:** `tests/`（およびその配下の `scenarios/`）ディレクトリには、学習の対象となるファイル（`config.dts`, `vfpga_top.v`, `main.c` 等）のみを配置し、FPGA-BoardlessBench (F-BB)特有の内部事情に関するファイル（例：シミュレータのC++ドライバなど）は絶対に配置しない。内部ロジックは `src/` や `scripts/` で隠蔽、または自動生成によって解決する。
  - **理由:** 学習者が「どこまでが一般的なFPGA/Linux開発の知識で、どこからがFPGA-BoardlessBench (F-BB)特有の仕組みなのか」を混同して混乱するのを防ぐため。学習のノイズとなる情報は裏側に隠し、本来の学習対象（Verilog, DTS, FW）に100%集中できるピュアな学習環境を維持する。

### 2. 主要なアーキテクチャ決定の記録 (Key Architectural Decisions)

本プラットフォームのこれまでの進化における主要な設計合意と決定（Decision）、およびその設計根拠（Rationale）の記録です。

> [!NOTE]
> **【歴史的変遷から読み解くプロジェクトの基本方針】**
> F-BB プラットフォームは、過去の数次にわたる意思決定を経て、以下の一貫した設計方針を確立しています：
> 1. **「実機透過性」の徹底**: システムコール Shim（LD_PRELOAD）による透過的インターセプト（I/O, ioctl, mount 等）や物理メモリのマッピングにより、ファームウェア側にシミュレーション用の条件分岐（`#ifdef` 等）を一切挟まず、実機と 100% 同一のバイナリを走行させる。また、Type-Safe な FD 管理（`FbbDeviceContext`）や適切なクローズフックによりリソース再利用時の不整合を排除する。
> 2. **「DTS 駆動」による一元化**: 周辺ペリフェラルの構成、レジスタ配置、論理名はすべて `config.dts` を「唯一の情報源」とし、共通の `DTSParser` を介した自動生成と動的バインドにより、ハード/ソフト間の仕様不一致を構造的に排除する。
> 3. **「疎結合ペリフェラル」とプラグイン化**: 各種バス（I2C/SPI/UART）のエミュレーターをコントローラから切り離し、独立したプロセス（プラグイン）として構築。コントローラ側は宣言的な `LAUNCHER_REGISTRY` マップを通じてこれらを動的にオーケストレーションし、オープン・クローズドの原則（OCP）を順守する。
> 4. **「環境依存の完全排除とシナリオの自己完結化」**: シミュレーション内部の定義（system）とシナリオ固有の定義（device）を分離し、絶対パスなどの環境依存をコンパイル定義へ追い出すことで、シナリオコードをそのまま実機開発へ移植できる高度な可搬性を保証する。
> 過去のすべての意思決定履歴については、**[AddInfo_history.md](./AddInfo_history.md)** を参照してください。

#### **ADR #004: マジックナンバーによる暫定ルーティングから、型安全な構造体コンテキスト管理（`FbbDeviceContext`）への昇華**
* **背景と課題**: 
  当初、C-Shim (`libfpgashim.c`) では仮想ファイル記述子 (FD) の判定として `-100` や `-200` といったマジックナンバーとインクリメントオフセットをフラットな配列として扱っていました。その後、Zynqのデバイス命名規則 (例: `/dev/i2c-0`) に合わせた動的アドレス抽出処理を追加した際、C言語の偽値判定 (0番ポート) の仕様によりバグが混入し、一時的に `bus_id + 1` という「その場しのぎのパッチ」で解決せざるを得ない技術負債が発生しました。
* **決定事項**:
  この状態はF-BBの基本理念である「堅牢性と実機透過性」に反するため、即座にマジックナンバー判定を全面廃止。`FbbDeviceType` 列挙型および `FbbDeviceContext` 構造体によるメモリ空間上の厳格な状態追跡エンジンへと大手術（リファクタリング）を行いました。また、OSがFDを再利用（リサイクル）した際の状態不整合を防ぐため、`close` システムコールもインターセプトしてSlot状態を自律クリーンアップする機構を組み込みました。
* **設計根拠**:
  「動けばよい」という場当たり的なパッチをそのまま放置することは、商用SoC開発に耐えうる信頼性を損なう重大なリスクとなります。型安全かつリソース競合に強い高信頼なインターセプト基盤を再構築し、開発者に対してプロ向けの技術的風格と説得力を示すための重大な意思決定です。


### 3. AIとの協調に関する指針 (AI Collaboration Policy)
- **未知の問題への対処:** 憲章にないデバイス（SPI, UART等）の追加が必要になった際、AIは既存のI2Cエミュレーションのパターンを継承し、複数のインターセプト案を提示すること。
- **検証の厳格化:** 実装されたShim関数は、必ず単体テスト（Cユニットテスト）と統合テスト（Pythonによる応答確認）をセットで生成すること。

### 4. コンポーネント設計仕様 (Component Design Specifications)

F-BB は、最下層の Verilog RTL 論理シミュレーションから、システムコール Shim 割り込み層、バックエンドオーケストレーター、そして最上層の React ダッシュボードに至るまで、以下の 7 レイヤーから構成されるフルスタックな疎結合アーキテクチャを採用している。

```mermaid
graph TD
    %% 1. FW Layer
    subgraph FW_Layer["Layer 1 FW and Application Layer - Host/Target Code"]
        ACore["A-Core Linux User Application<br/>e.g. test_bin C/C++"]
        MCore["M-Core Baremetal / RTOS<br/>e.g. FreeRTOS / ThreadX / Rust no_std"]
    end

    %% 2. Shim Layer
    subgraph Shim_Layer["Layer 2 Intercept and HAL - C Shim"]
        Shim["libfpgashim.so<br/>System Call Hook: open, mmap, ioctl, read, write, mount, umount"]
    end
 
    %% 3. Communication/Redirection
    subgraph Rel_Layer["Layer 3 Redirection Paths"]
        Path_MEM["Physical Address Map<br/>MAP_FIXED /dev/mem"]
        Path_Sock["UNIX Domain Sockets<br/>/tmp/fbb_spi_*, /tmp/fbb_i2c_*"]
        Path_PTY["PTY Pseudo Terminal<br/>UART Echo Bridge"]
        Path_Remoteproc["remoteproc Virtual Sysfs<br/>/sys/class/remoteproc/"]
        Path_Symlink["Symbolic Links<br/>Virtual SD Mount Redirect"]
    end
 
    %% 4. Control & IPC Layer
    subgraph Control_Layer["Layer 4 Orchestration and IPC - Python Backend"]
        SHM_UIO["UIO/GPIO Reg Shared Memory<br/>/tmp/uio, /tmp/gpio"]
        Controller["vlogic_controller.py<br/>remoteproc Monitor, Process Lifecycle Manager"]
        SD_Dir["Host SD Directory<br/>tests/scenarios/*/sd_card"]
    end
 
    %% 5. Peripherals (C++ processes)
    subgraph Peripheral_Layer["Layer 5 Virtual Peripherals - C++ Plugins"]
        V_I2C["fbb_i2c_eeprom"]
        V_SPI_Flash["fbb_spi_flash"]
        V_SPI_ADC["fbb_spi_adc"]
        V_UART["fbb_uart_loopback"]
        V_OLED["fbb_i2c_oled"]
    end
 
    %% 6. RTL Sim Layer
    subgraph RTL_Layer["Layer 6 RTL Simulation - Verilator Engine"]
        Sim_Main["vfpga_sim C++ wrapper"]
        RTL["vfpga_top Verilog logic"]
    end
 
    %% 7. Web Dashboard Layer
    subgraph Web_Layer["Layer 7 Visual Diagnostic UI - Web Dashboard"]
        WebServer["dashboard/server.js Node.js Express Socket.io"]
        ReactUI["Vite + React 19 Frontend<br/>Register Monitor, Tracer, SPI ADC Panel, HDMI Viewer, SD Explorer"]
    end
 
    %% Data/Control Flows
    ACore -->|ioctl / open / read / mount| Shim
    MCore -->|Direct Pointer Access / mmap| Shim
    
    Shim -->|Intercept UIO/GPIO| Path_MEM
    Shim -->|Intercept I2C/SPI| Path_Sock
    Shim -->|Intercept UART| Path_PTY
    Shim -->|Intercept remoteproc| Path_Remoteproc
    Shim -->|Intercept Mount/Umount| Path_Symlink
 
    Path_MEM --> SHM_UIO
    Path_Sock --> V_I2C
    Path_Sock --> V_SPI_Flash
    Path_Sock --> V_SPI_ADC
    Path_Sock --> V_OLED
    Path_PTY --> V_UART
    Path_Remoteproc --> Controller
    Path_Symlink -->|Symlink Redirect| SD_Dir
 
    V_SPI_ADC ---|SHM /spi_adc| WebServer
    V_OLED -->|SHM /fbb_display_0| WebServer
    SD_Dir -->|Read/Dump Files| WebServer
 
    SHM_UIO ---|Sync Regs/Clocks| Sim_Main
    Sim_Main --- RTL
    Controller -.->|Launch & Monitor| Sim_Main
    Controller -.->|Launch & Monitor| V_I2C
    Controller -.->|Launch & Monitor| V_SPI_Flash
    Controller -.->|Launch & Monitor| V_SPI_ADC
    Controller -.->|Launch & Monitor| V_UART
    Controller -.->|Launch & Monitor| V_OLED
 
    SHM_UIO -.->|Read Sync| WebServer
    WebServer ---|WebSocket| ReactUI
    V_UART ---|TCP Proxy Port 3000/3001| ReactUI
```

#### **Layer 1: FW & Application Layer**
*   **概要**: 実機と同一のソースコードでビルドされた Aコア Linux ユーザーアプリおよび Mコア ベアメタル/RTOS アプリ。
*   **詳細情報**: 各テストシナリオのファームウェア仕様については **[tests/scenarios/](./tests/scenarios/)** 配下の各シナリオ README を参照。

#### **Layer 2: Intercept & HAL Layer (C Shim)**
*   **概要**: システムコールをトラップし、仮想的な通信パスへ透過ルーティングする C Shim ランタイム。
*   **詳細情報**: コード生成テンプレートおよび生成方式については **[libfpgashim.c.template](./scripts/vfpga/templates/libfpgashim.c.template)** を参照。

#### **Layer 3: Redirection Paths (Communication Channels)**
*   **概要**: Shim層からリダイレクトされたデータを受け渡す物理メモリマップ、UNIXドメインソケット、PTY擬似端末等の通信路。
*   **詳細情報**: アドレスおよびポートマップの仕様については、各シナリオの **[config.dts](./tests/scenarios/02b_multi_spi/config.dts)** 等を参照。

#### **Layer 4: Orchestration & IPC Layer**
*   **概要**: シミュレーション環境全体のプロセスライフサイクル管理、remoteproc 状態監視、クロック同期エンジン。
*   **詳細情報**: 統合制御ロジックについては **[vlogic_controller.py](./src/controller/vlogic_controller.py)** を参照。

#### **Layer 5: Virtual Peripherals**
*   **概要**: I2C, SPI, UART などのプロトコルを模擬するプラグイン形式の C++ デバイスエミュレーター群。
*   **詳細情報**: 各周辺デバイスの実装については **[src/peripherals/](./src/peripherals/)** を参照。

#### **Layer 6: RTL Simulation**
*   **概要**: Verilator によってコンパイルされた RTL シミュレーション実行エンジン。
*   **詳細情報**: RTL ラッパーおよび HDL ロジックについては **[src/sim/](./src/sim/)** および **[src/rtl/](./src/rtl/)** を参照。

*   **概要**: Express サーバーおよび React 19 で構成される状態可視化・操作ダッシュボード。
*   **詳細情報**: ダッシュボードのセットアップおよびパネル仕様については **[dashboard/README.md](./dashboard/README.md)** を参照。

### 4.2. C-Shim システムコールフック＆型安全ルーティングのライフサイクル (C-Shim Interception & Routing Lifecycle)

F-BB の透過性を支えるシステムコール横取りと、ファイル記述子 (FD) の型安全状態管理およびリサイクル競合防止の全体像です。

```mermaid
sequenceDiagram
    autonumber
    participant FW as "Firmware (main.c)"
    participant Shim as "C-Shim (libfpgashim.so)"
    participant FD as "FD State Manager (FbbDeviceContext)"
    participant SHM as "Shared Memory (/tmp/vfpga_reg)"
    participant Daemon as "Loopback Daemon (PTY / Socket)"

    Note over FW, Shim: open() Interception
    FW->>Shim: open("/dev/ttyUL0", O_RDWR)
    Shim->>Shim: Detect match in DTS configuration
    Shim->>Shim: Allocate empty slot in fd_contexts[] (Type-Safe Enum)
    Shim->>FD: Set Context (Type: UART, Path: /dev/ttyUL0, Socket/PTY state)
    Shim-->>FW: Return virtual FD (e.g. 3)

    Note over FW, SHM: read() / write() Interception
    FW->>Shim: write(3, "data", 4)
    Shim->>FD: Lookup fd_contexts[3]
    alt Is UIO/GPIO (MMIO Address Space)
        Shim->>SHM: Direct memory access (MAP_FIXED)
    else Is SPI / I2C / UART
        Shim->>Daemon: Redirect via UNIX Socket or PTY Bridge
    end
    Shim-->>FW: Return success (bytes written)

    Note over FW, FD: close() and FD Recycle Protection
    FW->>Shim: close(3)
    Shim->>Shim: Intercept close()
    Shim->>FD: Clear fd_contexts[3] (Prevent FD reuse collision)
    Shim->>Shim: Call real close() syscall
    Shim-->>FW: Return success
```

### 5. 既知の未解決課題と保留事項 (Known Open Issues)
<!-- Issue: 割り込み(IRQ)の擬似通知, Status: 保留, Rationale: シグナルを用いるか、仮想fdへの書き込みを用いるか、性能評価後に決定する。 -->
<!-- Issue: 分散コンポーネントにおける自律的ライフサイクル管理, Status: 進行中 (暫定対処済), Rationale: 各コンポーネントが自身の所有しないリソース（例：Shim生成のUARTマップ）をクリーンアップしていたことによる競合。将来的には、start_lab.shによる中央集権的クリーンアップを廃止し、環境変数(VFPGA_CLEAN_BOOT等)を介した、各コンポーネントによる「自己所有リソースの自律的初期化」パターンへ移行すべきである。 -->

### 6. 予測される限界値
- **ビルド時間:** 数百万ゲート規模の巨大なデザインになると、Verilatorが生成する C++ のソースコードが肥大化し、ビルド時間が数十分に及び、ノート PC のメモリ（16GB〜32GB）を食いつぶす可能性があります。

- **アナログ/混載信号:** Verilator は 2 値（0/1）専用であるため、高精度なアナログ回路や X/Z 状態（ハイインピーダンス）の厳密な検証には向きません。


### 7. サポート状況とロードマップ (Support Status & Roadmap)
Zynq PS ペリフェラルの詳細な対応状況および将来の対応については、[ロードマップ](./docs/architecture/AddInfo_Loadmap.md) を参照してください。
