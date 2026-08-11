# シナリオ 06b: HUB75 64x64 RGB LED マトリクスパネル

## 概要
シナリオ `06b_hub75_matrix_64x64` は、PPA 4.0 の `generic_hub75_matrix64x64` プラグインアーキテクチャを使用した高速並列シフトレジスタ方式 RGB LED マトリクスパネルのエミュレーション環境を提供します。

![FPGA-BoardlessBench (F-BB) AroundView Dashboard](assets/dashboard.gif)

## デバイスツリー設定 (DTS)
```dts
hub75_matrix: matrix@e0005000 {
    compatible = "generic,hub75-matrix";
    reg = <0xe0005000 0x1000>;
    grid_size = <64 64>;
    shm_name = "fbb_hub75_0";
    status = "okay";
};
```

## 主な機能
- **64x64 RGB LED マトリックスエミュレーション**: `/dev/shm/fbb_hub75_0`（12,288 バイト）を介した 24ビット RGB フレームバッファのマッピング。
- **対話式 UART シェル**:
  1. レインボーカラーウェーブ (24-bit RGB Plasma Waves)
  2. バウンスボールシミュレーション (2D Physics & Particle Trail)
  3. スクロールバナーテキスト ("F-BB HUB75 64x64 Matrix")
  4. マンデルブロ集合 (Real-time Math Render)
  5. アニメーションフレームプレイヤー (Color Pattern Loop)
- **Web ダッシュボード統合**:
  - `[Screen]` モード: フルウィンドウ対応のレスポンシブドラッグスケーリングキャンバス。
  - `[PCB Board]` モード: 1:1 ベクトル回路基板 (`board.svg`) を統合したリアル表示。

## テストの実行方法

### 1. スタンドアロン自動テスト実行
```bash
./tests/scenario_runner.sh tests/scenarios/06b_hub75_matrix_64x64/
```

### 2. Web ダッシュボード付き対話開発ラボ起動
```bash
./start_lab.sh tests/scenarios/06b_hub75_matrix_64x64/
```
