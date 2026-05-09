# dashboard/ ARCHITECTURE_MANIFEST.md

## 1. 存在意義 (Core Value)
`dashboard/` は、物理的な計測器（オシロスコープ、ロジックアナライザ、シリアルコンソール）の仮想的な代替を提供し、学習者がハードウェアの内部状態を非侵襲的かつ直感的に把握できる **UX（ユーザー体験）の要** である。

## 2. 設計原則 (Design Principles)

### 2.1 Decoupled Observability (非侵襲的な観測)
- シミュレーションの実行ループ（Python/RTL）と可視化ループ（Node.js）をプロセスレベルで分離し、UI の描画負荷がハードウェアの論理動作に影響を与えないようにする。
- 観測は主に共有メモリ (SHM) のポーリングを通じて行い、ターゲットの実行を停止させない。

### 2.2 Adaptive Discovery (動的適応)
- ボード構成は `board_manifest.json` を唯一の正解とし、ハードウェア構成の変更（UARTの追加、GPIOビット幅の変更等）を、サーバーの再起動なしに UI へ反映する。
- **実装状況**: `server.js` 内でマニフェストファイルを定期的にポーリング・再ロードすることで、シナリオ切り替えや DTS 変更に即座に追従する動的環境適応を実現している。

### 2.3 Bridge Integration (統合された操作系)
- ログの閲覧だけでなく、UART を介した入力や、マクロによる操作自動化をサポートし、対話的なデバッグを可能にする。

## 3. 主要コンポーネント仕様

### 3.1 Dashboard Backend (`server.js`)
- **責務**: 
    - マニフェストの管理（動的リロード）。
    - SHM の定期監視と WebSocket (Socket.io) へのブロードキャスト。
    - フロントエンドからの **GPIO インジェクション**（トグル操作）のリクエストを SHM へ反映。
    - UART ブリッジ (PTY <-> TCP) の仲介。
- **データ構造**:
    - `shmBuffer`: SHM ファイルのメモリマッピング。
    - `uartConnections`: アクティブな UART ブリッジへの TCP ソケット。
- **アルゴリズム**:
    - **Physical-to-SHM Mapping**: `physAddr - minBaseAddr` を用いて、物理アドレスから SHM 内のバイトオフセットを算出する。
    - **Multi-Channel GPIO Identification**: AXI GPIO のデュアルチャネル構成（DATA/DATA2 等）をレジスタ名ベースで識別し、正しいメモリオフセットへ書き込みを行う。

### 3.2 Dashboard Frontend (React)
- **技術スタック**: Vite + React + Lucide-react (Icons) + Socket.io-client。
- **特徴**: 
    - 118 チャネルの GPIO をグリッド表示し、`TRI` レジスタの値に基づき LED（出力）とトグルスイッチ（入力）を動的に切り替えて描画。
    - CSS によるレスポンシブなサイドバー・ペインのリサイズ機能を備える。

## 4. 既知の未解決課題 (Known Open Issues)
- **多重接続時の競合**: 同一の UART に対して複数のブラウザタブから入力を試みた際の排他制御。
- **ヒストリカル・トレース**: レジスタ値の変化履歴（波形表示）のフロントエンドでの永続化。
