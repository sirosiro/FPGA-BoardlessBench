# dashboard/ ARCHITECTURE_MANIFEST.md

## 1. 存在意義 (Core Value)
`dashboard/` は、物理的な計測器（オシロスコープ、ロジックアナライザ、シリアルコンソール）の仮想的な代替を提供し、学習者がハードウェアの内部状態を非侵襲的かつ直感的に把握できる **UX（ユーザー体験）の要** である。

## 2. 設計原則 (Design Principles)

### 2.1 Decoupled Observability (非侵襲的な観測)
- シミュレーションの実行ループ（Python/RTL）と可視化ループ（Node.js）をプロセスレベルで分離し、UI の描画負荷がハードウェアの論理動作に影響を与えないようにする。
- 観測は主に共有メモリ (SHM) のポーリングを通じて行い、ターゲットの実行を停止させない。定期ポーリングループ（`updateShm`）は完全な Read-Only を徹底し、共有メモリファイルへの定期書き戻しは厳禁とする。

### 2.2 Adaptive Discovery (動的適応)
- ボード構成は `board_manifest.json` を唯一の正解とし、ハードウェア構成の変更（UARTの追加、GPIOビット幅の変更等）を、サーバーの再起動なしに UI へ反映する。
- **実装状況**: `server.js` 内でマニフェストファイルを定期的にポーリング・再ロードすることで、シナリオ切り替えや DTS 変更に即座に追従する動的環境適応を実現している。

### 2.3 Bridge Integration (統合された操作系)
- ログの閲覧だけでなく、UART を介した入力や、マクロによる操作自動化をサポートし、対話的なデバッグを可能にする。

## 3. 主要コンポーネント仕様

### 3.1 Dashboard Backend (`server.js`)
- **責務**: 
    - マニフェストの管理（動的リロード）。
    - SHM の定期監視と WebSocket (Socket.io) へのブロードキャスト（UIO, GPIO, DMA デバイス）。
    - **Register State Tracer**: UIO/GPIO/DMA レジスタ値の変化を検知し、最大500件のスナップショットを履歴として保持・配信。
    - フロントエンドからの **GPIO インジェクション**（トグル操作）のリクエストを、対象オフセットへの 4 バイト局所アトミック書き込み（`pwrite`）により SHM へ非侵襲的に反映（他レジスタへの競合・破壊を防止）。
    - **UART ブリッジ統合**: PTY <-> TCP (Port 2000~) の中継、外部ポート (Port 3000~) プロキシ、64KB リングバッファによる初期ブート/メニュー出力消失防止、および子プロセスへの `VFPGA_INTERACTIVE=1` 環境変数透過伝播。
    - **マルチコア・シナリオ・ライフサイクル管理 API**: 実行中のプロセス（Aコア/Mコア等）の自動検知 (`GET /api/scenario/cores`)、個別コアまたは全体の停止 (`POST /api/scenario/stop`)、およびカオス障害注入設定を反映した動的再起動 (`POST /api/scenario/restart`) を提供。
    - **カオス・エンジン・ログ監視**: `/tmp/fbb_chaos_injection.log` をポーリング監視し、発生した障害イベントを WebSocket (`scenario:chaos_event`) 経由でフロントエンドへ即時配信。
    - **レイアウト永続化 API**: シナリオフォルダの `fbb_layout.json`（またはスクリーン指定時は `fbb_layout_${screen}.json`）を通じてレイアウト情報をロード (`GET /api/layout?screen=...`) および保存 (`POST /api/layout?screen=...`) するエンドポイントを提供。
- **データ構造**:
    - `shmBuffer`: SHM ファイルのメモリマッピング。
    - `traceHistory`: レジスタ状態の時系列スナップショット配列。
    - `uartConnections`: アクティブな UART ブリッジへの TCP ソケット。
    - `currentChaosConfig`: カオス障害注入の動作状態・シード値・障害率・対象ペリフェラル辞書。

### 3.2 Dashboard Frontend (React)
- **技術スタック**: Vite + React 19 + Lucide-react (Icons) + Socket.io-client + **recharts** (Charting) + **dockview-react** (Docking Layout)。
- **アーキテクチャ特徴**:
    - **DPPA (Dashboard Pane Plugin Architecture)**: `paneRegistry.jsx` を介した宣言的ペイン管理機構。ペインの実装・定義と Dockview ウィンドウマネージャー（`App.jsx`）を完全疎結合化。
    - **ハイブリッド・マルチスクリーン (Pop-out & Multi-Screen Cockpit)**: Dockview のタブ右上に「Pop out to separate window」アクションを統合。React 19 `createPortal` による子ウィンドウ化および `BroadcastChannel` による画面間ステート同期をサポート。また、URL クエリ `?screen=<id>` / `?pane=<id>` によるモニタ別レイアウト永続化・単体全画面表示に対応。
    - **Chaos & Multi-Core Lifecycle Panel (`ChaosPanel`)**: シナリオ内の各コア（Aコア `test_bin` / Mコア `remoteproc*`）の稼働状態表示と個別選択再起動、シード指定モード（Off / Random / Fixed）、シード再ロール（Reroll）、CLI 再現用コマンド（`fbb test --chaos --seed ...`）のワンクリックコピー、障害率スライダー（0〜100%）、およびペリフェラル別（I2C, SPI, UIO, CAN, CDMA）障害有効化チェックボックスを統合した高機能検証ペイン。
    - **Register Monitor**: デバイス（モジュール）ごとにアコーディオンパネルで展開・折りたたみ可能にグルーピング表示。各レジスタに「Trace」チェックボックスを備え、Tracerでの描画および凡例の動的フィルタリングを双方向同期。
    - **GPIO / Pin Array**: 118 チャネルの GPIO をグリッド表示し、マニフェスト経由で配信される方向モード属性（`direction_mode`: `active_low_input` / `active_high_input`）に基づき LED（出力）とトグルスイッチ（入力）を完全なデータ駆動（SoC非依存）で動的に切り替えて描画。
    - **DTS Visualizer & AI Diagnostics**: 32-bit物理アドレス空間全体を一括俯瞰する「一体型メモリマップダイアグラム」を表示。マップ上の各デバイスブロックをクリックで直下にインライン展開し、アラインメント・レジスタ・プロパティを詳細確認可能。デバイス間の未割り当て空間（Unmapped Space Gap）をシックなグレーのパターン領域として可視化し、新規IP用空き容量バッファを提示。さらに Ollama LLM / CIP プロンプトと連動した「AI Smart DTS Check」により、コンパイルエラーの自然言語解説と推奨Fix Diffを提示。
    - **Register State Tracer**: レジスタの変化履歴を正規化表示し、微小な変化も可視化。凡例クリックまたは Register Monitor のチェックボックスと連動した、表示・非表示および凡例の動的な削除・追加に対応。
    - **IDE-style Docking Layout (Dockview)**: VS Code互換のドッキングレイアウトを採用。パネルのドラッグ＆ドロップによる分割・結合・タブ化・フローティング化をネイティブサポートし、UI全体の配置リセット機能（Reset Layout）も完備。レガシーな手動リサイズコードを撤去し、高精度なリサイズ体験を提供。
    - **Generic Peripheral View (PPA 2.0 Equalization)**: 公式・サードパーティを問わず全ペリフェラルプラグインを 100% 均一に扱う汎用ビューペイン。`fbb-plugin.json` 内で指定されたベクターグラフィック `board.svg` を解像度低下ゼロ（500%+ 拡大耐性）でレンダリングし、その上にリアルタイムフレームバッファ（Canvas Stream）やセンサー制御スライダー等を自動オーバーレイ描画。ペリフェラル非接続時は特定機器に偏らないスタイリッシュな **`NO ACTIVE PERIPHERAL CONNECTED` (スタンバイ画面)** を表示。
    - **レイアウト状態の保存・復元と個別ペイン追加機能**: 起動時に自動でサーバーからシナリオ別レイアウトを読み込み復元し、ヘッダーの「Save Layout」ボタンから現在の配置状態を保存可能。さらに **`+ Add Pane`** ドロップダウンメニューから、閉じた任意のペイン（または動的発見された複数ペリフェラルペイン）を選択して単体動的復元・フォーカス移動が可能。

## 3.3 UIレイアウトライブラリ選定意思決定ログ (UI Layout Library Decision Log)
F-BBのダッシュボード刷新にあたり、3つの選択肢（GoldenLayout, FlexLayout-React, Dockview）を比較・検討し、**Dockview**を採用しました。

#### 比較・評価のまとめ:
1. **GoldenLayout (評価: B - 非推奨)**:
   - *メリット*: 定番であり、ポップアウト（別ウィンドウ化）機能が強力。
   - *デメリット*: Vanilla JS向けコアのためReact 19と描画競合を起こしやすく、jQuery依存脱却の過程でバグが多く残っている。
2. **FlexLayout-React (評価: A- - 堅牢性重視)**:
   - *メリット*: 金融取引ツールでの実績が豊富。React専用設計で状態同期が完璧、堅牢で安定。
   - *デメリット*: デザインや操作性が少しクラシックであり、柔軟なアニメーションやVS Codeライクな直感性は劣る。
3. **Dockview (評価: A+ - 将来性・デザイン重視 / 採用)**:
   - *メリット*: HTML5のドラッグ＆ドロップAPIベースで、VS Codeとほぼ同等の非常に滑らかで洗練されたUXを実現。Vite + React 19のモダン環境に公式サポート。
   - *デメリット*: 比較的新しいライブラリであるため、超長期の枯れた安定性ではFlexLayoutに劣る。

#### 採用理由と将来へのトレードオフ:
F-BBは個人開発および教育的意義（お勉強）を持つプロダクトであり、「物理開発をソフトウェアに変革する」というモダンかつ先進的なコンセプトを掲げています。そのため、カチッとした堅牢性（FlexLayout）よりも、**VS Codeと同一の使用感・未来的なデザイン性（Dockview）がもたらす開発体験（DX）と学習のモチベーション**を最優先としました。
将来的にプロダクトのエンタープライズ化などで絶対的な堅牢性が求められる場合、またはReactの状態同期の完全性を高める場合は、コンポーネント構成はコンテキスト（`DashboardContext`）を介して既に抽象化・分離されているため、ドックのラッパー部分を **FlexLayout-React** へ移行するトレードオフが検討可能です。

## 3.4 複数UART外部接続方式の選定意思決定ログ (Multi-UART Connection Decision Log)
シリアルポート（UART）にTera Term等の外部ターミナルクライアントから直接接続しつつ、Webダッシュボード側でも通信内容を同時監視（スニッフィング）する要件に対し、2つのアーキテクチャ案を比較・検討し、**案B（Node.jsプロキシ方式）** を採用しました。

### 比較・評価のまとめ:
1. **案A (Pythonバックエンド・マルチキャスト方式)**:
   - *仕組み*: Python側のシリアルブリッジ (`vlogic_controller.py`) がマルチキャストサーバーとなり、Node.jsダッシュボードと外部のTelnetクライアントの双方向接続を同時に直接受け付けてブロードキャストする。
   - *メリット*: データフローの仲介者が減り、データレイテンシやNode.js側の負荷が軽減される。
   - *デメリット*: Pythonのソケット実装が複雑化し、シミュレーション側の論理動作に通信同期のオーバーヘッドや通信エラーによるブロックリスク（非侵襲性の破壊）が生じる。
2. **案B (Node.jsミドルウェア・TCPプロキシ方式 / 採用)**:
   - *仕組み*: Python側は従来のシンプルな「1ポート対1接続」を維持。Node.js (`server.js`) が外部接続ポート（`3000`/`3001`など）をリッスンするTCPサーバーとして仲介し、外部の入力データをPythonソケットへパイピングしつつ、送受信内容をWebSocket経由でブラウザへブロードキャストする。
   - *メリット*:
     - **疎結合の維持**: シミュレーション側の制御ループ (Python) は通信の複雑さから隔離され、リアルタイム性が侵されない。
     - **ダッシュボード側の機能強化**: 接続時のログ再生（リプレイ）やダッシュボード入力のエコーバック（画面同期）など、ユーザーのデバッグ体験をNode.js側で柔軟にカスタマイズ・実装できる。
     - *デメリット*: Node.jsサーバーがデータ中継のオーバーヘッドを背負う。

### 採用理由と将来へのトレードオフ:
F-BBの設計原則である **「Decoupled Observability (非侵襲的な観測)」** を守るため、通信の多重化や画面同期といった「プレゼンテーション層に近い複雑さ」はシミュレーションエンジン（Python/RTL）に持ち込まず、ミドルウェアである Node.js（`server.js`）にすべて引き受けさせるべきであると判断しました。
Node.js 側のメモリやCPUのオーバーヘッドは、組み込みデバッグ用途のシリアルデータ通信（数Kbps〜数十Kbps程度）においては無視できるほど軽微であり、アーキテクチャのクリーンさとデバッグ機能の柔軟性というメリットが大幅に勝るため、案Bを採用としました。

## 3.5 GPIO レジスタマッピングと入出力分離アーキテクチャ防護規約 (GPIO Critical Section Guard Protocol)

GPIO の方向判定およびデータレジスタ特定ロジック（`GpioPanel.jsx` / `server.js`）は、過去の歴史の中で類似のエンバグが3回発生した**最重要クリティカルセクション**である。今後の開発や AI エージェントによるリファクタリングにおいて、以下の不変条件（Invariants）を厳格に順守すること。

### 過去 3 回のエンバグの教訓 (Historical Context)
1. **第 1 世代 (SoC 固有ハードコード)**: NXP 専用のレジスタ名 `PDIR` / `PDOR` を UI に直書きしていたため、Zynq 以外の多様な SoC への拡張性を損なっていた。
2. **第 2 世代 (不完全なパターンマッチング)**: 汎用化を狙って `r.name.includes('IN')` に書き換えたが、NXP の `PDIR`（Port Data **I**nput **R**egister）には連続した `"IN"` が含まれないためマッチ失敗。`dataInReg` が常に出力レジスタ `PDOR` にフォールバックする潜在バグが発生。
3. **第 3 世代 (粗暴な上書きバグによるマスキングと局所化での顕在化)**: 旧 `server.js` が共有メモリ全体をファイルへ定期強制上書きしていたため、`forEach` で `PDIR` にも偶然ビットが書き込まれて動いていた。DMA 破壊防止のため 4 バイト局所アトミック書き込み（`pwrite`）へ改善した際、UI が誤要求していた `PDOR` にのみ書き込まれ、FW（`imx_hal.cpp`）が監視する `PDIR` に値が届かなくなる問題として顕在化した。

### アーキテクチャ不変条件 (Invariants & Protocols)
- **Single Source of Truth (DTS 駆動)**:
  - 単一レジスタ構成（Zynq 等）: DTS では `"DATA @ 0x00"` と定義。
  - 入出力分離型構成（i.MX95 等）: DTS の括弧記法を用いて `"PDOR(DATA_OUT) @ 0x00"`, `"PDIR(DATA_IN) @ 0x04"` と論理名を明示的に宣言する。
  - 方向極性反転: DTS で `"PDDR(INV_TRI) @ 0x08"` と定義し、RTL ではなく UI 側でアクティブローとして解釈する。
- **UI のレジスタ特定責務 (`GpioPanel.jsx`)**:
  - `dataOutReg`: 論理名 `DATA_OUT`、または業界標準命名（`OUT`, `ODR`, `DOUT`）を優先。
  - `dataInReg`: 論理名 `DATA_IN`、または業界標準命名（`IN`, `IDR`, `DIN`）を優先。
  - **ピン操作時の契約**: 入力ピンのトグル操作（`handleGpioToggle`）では、必ず `dataInReg.name` を引数としてバックエンドへ渡すこと。絶対に出力レジスタ名を渡してはならない。
- **バックエンドの局所書き込み責務 (`server.js`)**:
  - `injectGpioPin` は対象オフセットの 4 バイトのみを局所更新する。
  - レジスタ特定時は、入力レジスタ（`DATA_IN`, `IN`, `IDR`, `DIN`）を優先して解決し、ファームウェアのポーリング先へ確実に値を伝達すること。

## 3.6 DPPA (Dashboard Pane Plugin Architecture) およびハイブリッド・マルチスクリーン選定意思決定ログ (DPPA & Multi-Screen Architecture Decision Log)

### 1. 背景と課題 (Why)
- **密結合の技術的負債**:
  - レガシーな `App.jsx` は、全ペインのコンポーネント（RegisterMonitor, GpioPanel, TracerPanel, UartPanel, DtsVisualizerPanel, ChaosPanel, GenericPeripheralPanel）、アイコン（Lucide）、メニュー構造、Dockviewコンポーネント辞書を直接内部で抱え込んでいた。
  - ペインが1つ追加・変更されるたびに、`App.jsx` のインポート文、メニューカテゴリ配列、Dockview初期化ブロックを修正する必要があり、開放閉鎖原則（OCP: Open-Closed Principle）に著しく違反していた。
- **単一画面（シングルモニタ）の手狭さ**:
  - 物理FPGAボードの代替として高機能化するにつれ、シリアルコンソール（UART1/2）、波形ロジックアナライザ（Tracer）、レジスタ監視、メモリマップ（DTS）、カオス注入パネル、ペリフェラルビューとペイン数が倍増。
  - 複数モニタを持つ開発者が「1画面に全ペインを詰め込む」ことを強いられ、作業領域が圧迫されていた。

### 2. バックエンド PPA (ADR #005) とフロントエンド DPPA の設計思想的一致
- バックエンド側では、ADR #005（Peripheral Plugin Architecture）により、特定ペリフェラルの知識をコア（`vlogic_controller.py`）から排除し、`fbb-plugin.json` による自動探索と動的マッピングを実現した。
- 今回のフロントエンド刷新では、このプラグイン指向の設計原則をUI層へ水平展開し、**DPPA (Dashboard Pane Plugin Architecture)** を確立した。
- `App.jsx` を純粋な「Docking Window Manager & Layout Coordinator」へと責務を絞り込み、ペインの定義・カテゴリ・アイコン・ファクトリを宣言的レジストリ（`paneRegistry.jsx`）へと移譲した。

### 3. マルチスクリーン構成の比較と選定
1. **案A: Electron / Tauri 等のネイティブデスクトップアプリ化 (評価: C - 不採用)**:
   - *理由*: Docker コンテナ開発および「Web標準・ブラウザだけで即動く」という F-BB の根本設計原則（Decoupled Observability & Zero Heavy Install）を破壊する。クロスプラットフォームのビルドやインストールの手間を学習者に強要する。
2. **案B: 単純な複数タブ/独立URLルーティング方式 (評価: B - 不採用)**:
   - *理由*: 各タブが独立したSocket.io接続とReactステートを持つため、状態同期が取れず、メインウィンドウからの「ワンクリック切り離し（Pop-out）」や「再格納（Re-dock）」というシームレスなDXが実現できない。
3. **案C: Web標準ハイブリッド・マルチスクリーン（React 19 `createPortal` + `BroadcastChannel` / 評価: A+ - 採用）**:
   - *仕組み*:
     - **動的 Pop-out (子ウィンドウ化)**: Dockview の各タブ右上に配置された「Pop-out」ボタンをクリックすると、`window.open` で新規ウィンドウを生成し、React 19 の `createPortal` を介して親ウィンドウのコンポーネントツリーから透過的に子ウィンドウのDOMへ描画。親のスタイル（CSS/ダークテーマ）を自動複製し、子ウィンドウを閉じれば親Dockviewの元位置へ自動リドック。
     - **URL クエリパラメータによるモニタ別コックピット化 (`?screen=<id>`)**: サブモニタ専用のブラウザウィンドウで `http://localhost:8080/?screen=monitor2` を開くことで、モニタごとに独立したレイアウトファイル（`fbb_layout_monitor2.json`）を永続化・ロード可能。
     - **単体ペイン全画面ビュー (`?pane=<id>`)**: プロジェクター投影や専用モニタ全画面化向けに、Dockview ヘッダーのない単一ペイン全画面モード（例: `?pane=peripheral_ssd1306`）を提供。
     - **ゼロ・オーバーヘッド画面間同期 (`BroadcastChannel`)**: 複数画面間でUI設定（Tracerの非表示キー等）を共有するため、サーバーを介さずブラウザ標準の `BroadcastChannel('fbb_dashboard_sync')` を使用。

### 4. 各ソースコードの役割と責務 (Source File Roles & Responsibilities)
- **`dashboard/client/src/panes/paneRegistry.jsx`**:
  - 宣言的ペインレジストリ。`BUILTIN_PANES` 配列で全ペインのメタデータ（id, title, category, icon, component, defaultLocation）を一元定義。
  - `getGroupedPanesForMenu(manifest)`: メニュー用ツリーの動的抽出。
  - `getDockviewComponentsMap()`: Dockview 用コンポーネントマップの自動生成。
  - `registerCustomPane(definition)`: アドオンペイン（将来の ROS 2 AMR Nav2 Map 等）用動的拡張フック。
- **`dashboard/client/src/components/PopoutWindow.jsx`**:
  - React 19 `createPortal` によるマルチウィンドウポータルマネージャー。親の `<style>` / `<link>` の子ウィンドウへの完全同期、ダークテーマのクラス伝播、ヘッダーの「Re-dock to Main Window」ボタンおよび `beforeunload` での親Dockviewへの自動リドック処理をカプセル化。
- **`dashboard/client/src/components/DockHeaderActions.jsx`**:
  - Dockview の `rightHeaderActionsComponent` にマウントされるタブヘッダー拡張。現在アクティブなタブを検出し、VS Code 風の「Pop out to separate window」アイコンボタンを表示、クリック時にカスタムイベント `fbb:popout` を発火。
- **`dashboard/client/src/components/DashboardContext.jsx`**:
  - WebSocket（Socket.io）および共有メモリ状態のマスタープロバイダー。`BroadcastChannel('fbb_dashboard_sync')` を内包し、別ウィンドウとして開かれた複数画面間でのUI状態（非表示トレースキー等）のゼロレイテンシ同期を仲介。
- **`dashboard/client/src/App.jsx`**:
  - ペインの実装詳細から完全に切り離された「純粋なドッキング・ウィンドウマネージャー」。レジストリからの情報に基づき、Dockview のレイアウト制御、ポップアウト状態管理（`poppedOutPanels`）、URLクエリパラメータ（`?screen=`, `?pane=`）の解釈、およびレイアウト保存/復元を統括。
- **`dashboard/server.js`**:
  - `GET /api/layout` および `POST /api/layout` で `?screen=<id>` クエリパラメータを受け付け、`fbb_layout_${screen}.json` の永続化をサポート（デフォルト時は従来の `fbb_layout.json` を維持し完全な下位互換性を担保）。

### 5. 狙いと効用 (Benefits & Impact)
- **開閉原則 (OCP) の完全遵守**: 新規ペイン（P02のROS 2 AMR Nav2 Map等）を追加する際、`App.jsx` の修正は一切不要。`paneRegistry.jsx` に1エントリ追加するだけで、メニュー・Dockview・ポップアウト・個別URLの全機能が自動的に有効化される。
- **マルチモニタ作業効率の大幅向上**: シリアルコンソールや波形トレーサー、ペリフェラルモニタを物理的な別モニタへ切り離し、メイン画面のドッキングレイアウトと並行して広々とデバッグ可能。
- **ゼロ・オーバーヘッド・ゼロ・インストール**: 外部ネイティブアプリのインストールが不要で、Dockerコンテナ環境およびブラウザのWeb標準機能のみで完結。

## 4. 既知の未解決課題 (Known Open Issues)
- **多重接続時の競合**: 同一の UART に対して複数のブラウザタブから入力を試みた際の排他制御。
- **波形エクスポート**: トレーサーで記録したデータの CSV/JSON 形式でのダウンロード機能。
- **シナリオ別UIレイアウトの保存と復元（マルチスクリーン対応）**: 各シナリオフォルダ内の `fbb_layout.json` / `fbb_layout_${screen}.json` を通じたペインレイアウトの永続化。[NEW - マルチスクリーン対応完了]

