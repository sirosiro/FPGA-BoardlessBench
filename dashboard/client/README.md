# F-BB ダッシュボード フロントエンド (Client)

このディレクトリは、仮想 FPGA 診断ダッシュボード（F-BB Web Dashboard）のフロントエンド（React / Vite）のソースコードを収めています。

## 1. 技術スタック (Tech Stack)

- **フレームワーク**: React 19
- **ビルドツール**: Vite 8
- **レイアウトエンジン**: [Dockview](https://dockview.dev/) (`dockview-react`) - VS Codeライクな分割・結合ドラッグ対応レイアウトを提供。
- **データ可視化**: Recharts (`recharts`) - レジスタ値の時系列トレースを正規化してグラフ描画。
- **リアルタイム通信**: Socket.io-client - バックエンドとの双方向 WebSocket 接続。
- **アイコン**: Lucide React (`lucide-react`)

---

## 2. コンポーネント構造 (Component Structure)

コードベースは保守性と可読性を向上させるため、機能ごとに個別コンポーネントへ分割されています。

- **`App.jsx`**: ダッシュボードのメインエントリーポイント。Dockview のインスタンス初期化と初期レイアウト定義、レイアウトリセットハンドラを管理。
- **`App.css`**: Dockview の CSS 変数のオーバーライド（F-BBダークテーマ用）および共通コンポーネントのスタイリング。
- **`components/DashboardContext.jsx`**: 
  - フロントエンド全体のグローバル状態（レジスタ値、GPIO値、UARTログ、WebSocket接続状態など）を一元管理する React Context。
  - 各パネルからは `useDashboard()` フックを呼び出して、クリーンに状態を取得・操作可能。
- **`components/RegisterMonitor.jsx`**: ボード上の内部レジスタ名、オフセット、現在の値をリアルタイムに監視するテーブル。
- **`components/GpioPanel.jsx`**: 
  - UIO/GPIOデバイスの入出力ビットピンアレイ（118チャネル）を描画するグリッドパネル。
  - 方向レジスタ（TRI や GDIR など）の論理名に基づき、LED表示（出力）とトグルスイッチ表示（入力・インジェクション対応）を切り替え。
- **`components/UartTerminal.jsx`**: 
  - UARTデバイス（シリアルコンソール）ごとのタブ切り替えとログ出力、およびコマンド送信を行うターミナル。
- **`components/RegisterTracer.jsx`**: 
  - 観測対象レジスタの変化履歴を Recharts で可視化した時系列ラインチャート。凡例クリックによる表示トグルをサポート。

---

## 3. 開発およびビルドコマンド

本ディレクトリ (`dashboard/client`) 内で以下の npm スクリプトを実行できます。

### 依存関係のインストール
```bash
npm install
```

### 開発用サーバーの起動
ローカルで HMR (Hot Module Replacement) を有効にして開発サーバーを立ち上げます。
```bash
npm run dev
```

### 本番用ビルドの作成
アセットをコンパイル・ミニファイし、`dist/` ディレクトリに出力します。ダッシュボードサーバー (`server.js`) はこの `dist/` ディレクトリの中身を静的ファイルとして配信します。
```bash
npm run build
```

### リンターチェックの実行
ESLint によるコード規約チェックを実行します。
```bash
npm run lint
```
