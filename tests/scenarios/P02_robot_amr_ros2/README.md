# P02_robot_amr_ros2: 自律移動ロボット (AMR) × ROS 2 統合制御 & Web コックピット

本シナリオは、車載 SurroundView 画像処理シナリオ（`P01_frdmIMX`）に続く、ロボティクス分野のフラッグシップ実践プロジェクトです。

差動二輪ロボット（AMR: Autonomous Mobile Robot）の運動学・オドメトリ積算エンジン、FPGA（Verilator）の 1kHz 周期割り込み・デュアル PWM・直交エンコーダ回路、Linux UIO (`/dev/uio0`) ハードウェア抽象化レイヤ（`ros2_control`）、そして Web ダッシュボードの 4つの特化型ペインを統合した環境を提供します。

![FPGA-BoardlessBench (F-BB) AMR ROS 2 Actuator Cockpit Dashboard](assets/dashboard.gif)

---

## 1. シナリオの特徴と学習ポイント

1. **実機標準のロボティクス制御をボードレスで体験**:
   - AMD Xilinx Kria KR260 や Zynq-7000 等の実機 FPGA ボードと同じ Linux UIO インターフェース（`/dev/uio0`）を使用。
   - 物理ボードや実機モータが手元になくても、PC 上で実機と全く同じ C++ 制御ソフトウェアを開発・検証できます。
2. **ゼロ・ヘビーインストール設計 (Learner-Centric Purity)**:
   - 10GB を超える重厚な ROS 2 デスクトップ環境を一切必要とせず、自己完結した軽量互換ヘッダーにより数秒でビルド・実行できます。
   - 実機 ROS 2 / `colcon` 環境が存在する場合は自動検出し、公式の `hardware_interface` ライブラリと透過的にリンクします。
3. **ブラウザ連動のリアルタイム・コックピット**:
   - Web ダッシュボード（ポート 8080）と WebSocket で接続し、ロボットの 2D 走行軌跡やモータ速度追従波形、1kHz 制御ジッターをリアルタイムに可視化・操縦できます。

---

## 2. クイックスタート (Quick Start)

### ① 自動テストハーネスの実行（全6基準の検証）
リポジトリルートから以下のスクリプトを実行します：

```bash
./tests/scenario_runner.sh tests/scenarios/P02_robot_amr_ros2
```

実行後、ロボティクス制御の 6つの重要判定基準（1kHz ジッター、車輪対称性、超信地旋回、2D オドメトリ積算精度、E-STOP 即時遮断、32-bit レジスタ保護）が自動検証され、ミリ秒単位で合否判定（PASS/FAIL）が出力されます。

### ② インタラクティブ・ラボの起動（Web ダッシュボード連携）
Web ダッシュボードと連携して、仮想ジョイスティックやキーボード操作でロボットを走行させる場合：

```bash
./start_lab.sh tests/scenarios/P02_robot_amr_ros2
```

起動後、ブラウザで **`http://localhost:8080`** を開くと、`fbb_layout.json` に基づき 4つの特化型コックピットペインが自動配置されます。各ペイン右上の「Pop-out」ボタンで別ウィンドウへの切り離し表示も可能です。

---

## 3. Web コックピット (4つの特化型ペイン)

従来の ROS 2 開発で複数立ち上げていた CLI / GUI ツール群を、ブラウザ上に集約しています：

| ペイン名 | 対応する従来ツール | 主な機能と観察ポイント |
| :--- | :--- | :--- |
| **`ros2PoseMap2D`** | `rviz2` (2D Nav Goal) | 2D Canvas によるロボットの現在位置 $(x, y)$・進行方向・累積走行軌跡のリアルタイム描画 |
| **`ros2JointWaveform`** | `rqt_plot` | Recharts による左右輪「目標速度 vs 実測エンコーダ速度」追従波形および PWM 出力グラフ |
| **`ros2ControlStatus`** | `ros2 control list_...` | `ros2_control` のライフサイクル状態、エクスポートされたインターフェース値、1kHz ジッター計測 |
| **`ros2TeleopConsole`** | `teleop_twist_keyboard` | 仮想ジョイスティック、キーボード（WASD）、テスト走行ミッションボタン、ハードウェア E-STOP |

---

## 4. システムアーキテクチャ & ソースコード構成

### モジュール間連携データフロー

各モジュールは関心の分離（Separation of Concerns）に基づいて独立設計されており、明確なインターフェースを介して疎結合に連携します：

```mermaid
graph LR
    subgraph UI ["Web Dashboard (Browser)"]
        CmdJson["/tmp/fbb_amr_cmd.json<br/>(v, w, estop)"]
        TelemJson["/tmp/fbb_amr_telemetry.json<br/>(Pose, Jitter, Joints)"]
    end

    subgraph App ["main.cpp (Test Harness / Daemon)"]
        Loop["50Hz Telemetry Loop"]
    end

    subgraph Kin ["amr_kinematics (数学モデル)"]
        InvKin["逆運動学 (Twist -> Wheel Speed)"]
        FwdOdom["2D オドメトリ (RK2 積算)"]
    end

    subgraph HW ["uio_robot_hardware (ros2_control)"]
        SysIf["hardware_interface::SystemInterface"]
        Jitter["1kHz Jitter 計測 & 割り込み同期"]
    end

    subgraph FPGA ["FPGA RTL (/dev/uio0)"]
        UIO["Zynq UIO MMIO (0x40000000)<br/>PWM / QEI Encoder / E-STOP"]
    end

    CmdJson -->|Non-blocking Read| Loop
    Loop -->|Target Twist| InvKin
    InvKin -->|Wheel Cmds| SysIf
    SysIf -->|PWM / ESTOP| UIO
    UIO -->|QEI Ticks / IRQ| SysIf
    SysIf -->|Delta Ticks| FwdOdom
    FwdOdom -->|Pose x,y,theta| Loop
    SysIf -->|Jitter / State| Loop
    Loop -->|Atomic Export| TelemJson
```

### ソースコード一覧と役割

| ファイル | 役割 | 初学者が押さえるべきポイント |
| :--- | :--- | :--- |
| **[`main.cpp`](main.cpp)** | 最上位アプリ | 自動テストハーネスの合否判定と、Web UI 向け 50Hz テレメトリ出力デーモンを自律切り替え。 |
| **[`amr_kinematics.hpp`](amr_kinematics.hpp)<br>[`amr_kinematics.cpp`](amr_kinematics.cpp)** | 差動二輪運動学 | 純粋な C++20 数学ライブラリ。車体速度 $(v, \omega)$ と車輪速度の相互変換、ルンゲ・クッタ 2次オドメトリ積算を担当。 |
| **[`uio_robot_hardware.hpp`](uio_robot_hardware.hpp)<br>[`uio_robot_hardware.cpp`](uio_robot_hardware.cpp)** | ハードウェア抽象化 | Linux UIO（`/dev/uio0`）経由で FPGA と通信する公式 `ros2_control` 仕様のハードウェアプラグイン。 |
| **[`vfpga_top.v`](vfpga_top.v)** | FPGA 回路 (RTL) | 1kHz 周期タイマ割り込み、モータ力学・エンコーダパルス積算、およびハードウェア E-STOP をエミュレート。 |
| **[`include/hardware_interface/...`](include/hardware_interface/hardware_interface.hpp)** | ゼロインストール層 | 重厚な ROS 2 パッケージなしでビルド可能にする互換レイヤー。実機では公式ライブラリに無変更で置換可能。 |
| **[`amr_manifest.json`](amr_manifest.json)** | Single Source of Truth | ロボットのトレッド幅、車輪半径、エンコーダ分解能、ミッション定義を一元管理。 |

---

## 5. ロボティクス設計規約・境界前提 (System Conventions & Boundaries)

ロボティクスソフトウェアと組込みハードウェア・コシミュレーションを繋ぐ境界における 4 つの基本規約です：

1. **座標系と単位系（[REP-103](https://www.ros.org/reps/rep-0103.html) 準拠）**:
   - **右手系**: 前方 $+X$、左方 $+Y$、鉛直上方 $+Z$。
   - **回転角**: $Z$ 軸正方向から見て反時計回り（CCW）が正（左旋回時に旋回角速度 $\omega > 0$）。
   - **SI 単位の徹底**: 距離はメートル（$\text{m}$）、角度はラジアン（$\text{rad}$）、速度は $\text{m/s}$ および $\text{rad/s}$。
2. **時間モデル（FPGA タイムマスター方式）**:
   - FPGA 側の 1kHz ハードウェアタイマを同期源（Time Master）とし、Linux 側は割り込み駆動（受動的起床）で動作することで、速度微分ノイズを排除します。
3. **アクチュエータ制御階層**:
   - 上位の `ros2_control` から目標車輪速度を受け取り、ハードウェア層で 16-bit PWM 比（$-1000 \sim +1000$）に変換して FPGA に書き込みます。
4. **フェイルセーフと安全保護**:
   - ソフトウェアがクラッシュしても、FPGA のハードウェア論理回路がモータ出力を即座に 0V へ強制遮断する E-STOP 機構を備えています。

---

## 6. UIO レジスタマップ仕様書 (`vfpga_robot_uio@40000000`)

[config.dts](config.dts) で定義されている物理アドレス `0x40000000` のレジスタ配置：

| オフセット | レジスタ名 | 属性 | ビット幅 | 詳細仕様 |
| :---: | :--- | :---: | :---: | :--- |
| `0x00` | **`STATUS`** | RO | 32 | モータドライバステータス（bit[0]: FAULT, bit[1]: ENABLED, bit[2]: IRQ_PENDING） |
| `0x04` | **`LEFT_ENCODER`** | RO | 32 (signed) | 左輪直交エンコーダ累積パルス（32bit 符号付きカウンタ） |
| `0x08` | **`RIGHT_ENCODER`**| RO | 32 (signed) | 右輪直交エンコーダ累積パルス（32bit 符号付きカウンタ） |
| `0x0C` | **`INT_ACK`** | WO | 32 | 1kHz タイマ割込クリアレジスタ（bit[0] に 1 を書き込んでクリア） |
| `0x10` | **`CONTROL`** | RW | 32 | 制御レジスタ（bit[0]: RUN, bit[1]: ESTOP, bit[2]: RESET, bit[3]: TEST_LOAD） |
| `0x14` | **`LEFT_PWM`** | RW | 32 (signed) | 左輪目標 PWM デューティ比（-1000 〜 +1000） |
| `0x18` | **`RIGHT_PWM`** | RW | 32 (signed) | 右輪目標 PWM デューティ比（-1000 〜 +1000） |
| `0x1C` | **`CYCLE_CNT`** | RO | 32 | 1kHz 周期フリーランニングサイクルカウンタ |

---

## 7. さらに詳しく知りたい方へ (ADVANCED.md へのステップアップ)

実機量産開発や高度なロボティクス工学に踏み込みたい方は、**[ADVANCED.md](ADVANCED.md)** をご覧ください。以下の詳細設計が網羅されています：

- **異種ヘテロジニアス SoC の制御階層設計**: Aコア (Linux) vs Mコア (RTOS) vs FPGA の役割分担と多軸・高速制御（FOC）トレードオフ
- **FPGA RTL 内部回路設計**: `vfpga_top.v` のブロックダイアグラム、1kHz タイマ割込回路、ハードウェア E-STOP、32-bit ラップアラウンド耐性試験 (`TEST_LOAD`)
- **差動二輪運動学と高精度オドメトリ数理**: 逆運動学・順運動学の完全な導出、ルンゲ・クッタ 2次（中点法）の幾何学的誤差抑制
- **UIO ハードウェア抽象化層 & 実機移植ノウハウ**: 2の補数ラップアラウンド代数計算、非RT仮想化環境におけるタイマージッター耐性設計
- **テレメトリ・IPC パイプライン**: 不正入力に強い堅牢なパース (`safe_stod`) とアトミックリネームによる破損防止
- **境界領域ノウハウ (Boundary Domain)**: $SE(2)$ 同次変換行列、Sim Time / Wall Time 同期モデル、多段カスケード制御、産業ロボット機能安全 (ISO 3691-4 / STO)

---

## 8. 公式リファレンス・推薦学習リソース (References)

- **[ROS 2 Documentation](https://docs.ros.org/)** (Open Robotics 公式) — ROS 2 アーキテクチャと `ros2_control` 仕様の世界標準ポータル。
- **[Modern Robotics](http://modernrobotics.org/)** (Northwestern Univ. / Prof. Kevin M. Lynch) — ロボット機構学、運動学、動力学の数理背景とオープンソース実装ハブ。
- **[制御工学チャンネル](https://www.portal.control-theory.com/)** (南裕樹 教授 運用ポータル) — フィードバック制御、PID制御、状態空間モデルを平易に解説した日本語ポータル。
