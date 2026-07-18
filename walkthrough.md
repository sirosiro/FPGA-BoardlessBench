# ワークスルー - A-Core 仮想SDカード・ローカルディレクトリマウントの実装と検証 (更新版)

本作業では、A-core（Linuxアプリケーション）側のSDカードブロックデバイス（`/dev/mmcblk0p1`）に対するマウントおよびアンマウント処理をF-BBのC-Shim（`libfpgashim.so`）でフックし、ホスト側の仮想SDディレクトリへ透過的にバインドマウント（シンボリックリンクによるリダイレクト）する機能を実装しました。また、ダッシュボード上にSDカードのマウント状況、ディスク容量メーター、ファイル一覧、およびファイル中身のText/HEX切り替えダンプ機能を追加しました。

さらに、ユーザーからの追加要望に基づき、**対話的なステップ実行が可能なUARTテストメニュー**と**画面分割レイアウト**を導入しました。

---

## 変更内容一覧

### 1. 【C Shim】マウントシステムコールのフック
- **[libfpgashim.c.template](file:///workspaces/FPGA-BoardlessBench/scripts/vfpga/templates/libfpgashim.c.template)**
  - `mount`, `umount`, `umount2` のフック関数を追加。
  - アプリケーションが `/dev/mmcblk0*` をマウントする際、環境変数 `FBB_SD_DIR` (またはデフォルトの `sandbox/sd_card`) をターゲットのパス（`/mnt/sd` 等）へシンボリックリンクします。
  - アンマウント時には、作成されたシンボリックリンクを安全に削除（`unlink`）してクリーンアップします。

### 2. 【ダッシュボード・サーバー】SD管理APIの追加
- **[server.js](file:///workspaces/FPGA-BoardlessBench/dashboard/server.js)**
  - `/api/sdcard/status`: マウント有無（シンボリックリンクの存否）、使用量（ディレクトリ内の全ファイルサイズの合算）、仮想容量（512MB）情報を返します。
  - `/api/sdcard/list`: 仮想SDフォルダ内の全ファイルのエントリリスト（ファイル名、サイズ、更新日時）を返します。
  - `/api/sdcard/dump`: クエリパラメータ `?file=xxx&format=<text|hex>` に基づき、対象ファイルを文字列または16進数（Hex Dump）形式で整形して返します。
  - **インテリジェント自動フォールバック**: `FBB_SD_DIR` が未指定の場合、現在起動しているアクティブなシナリオ（`manifest.scenario_dir`）配下の `sd_card` フォルダを自動検出して優先参照します。

### 3. 【ダッシュボード・UI】React パネルと画面分割レイアウト
- **[SdCardPanel.jsx](file:///workspaces/FPGA-BoardlessBench/dashboard/client/src/components/SdCardPanel.jsx)** [NEW]
  - マウント時発光インジケーター、ディスクメーター、ファイルテーブル、および「Text / HEX」表示切り替えタブ付きの等幅フォントファイルプレビューアを構築。
- **[App.jsx](file:///workspaces/FPGA-BoardlessBench/dashboard/client/src/App.jsx)**
  - `sdCard` コンポーネ实现を登録し、Dockviewレイアウトへ追加。
- **[fbb_layout.json](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/19_acore_sd/fbb_layout.json)**
  - ダッシュボードの標準配置を左右2分割にし、**左側に仮想SDカードビューア、右側にUARTシリアルターミナル**を同時に配置しました。

### 4. 【DTSおよびテストプログラム】対話型UARTメニューの実装
- **[config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/19_acore_sd/config.dts)**
  - `/dev/ttyPS2` としてアクセス可能な `xlnx,xps-uartlite-1.00.a` シリアルコントローラデバイス定義を追加しました。
- **[main.c](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/19_acore_sd/main.c)**
  - **自動モード**: `VFPGA_INTERACTIVE` 環境変数が未指定時（自動回帰テストなど）は、Mount ➔ Write ➔ Verify ➔ Umount を自動で一貫実行して終了します。
  - **対話モード**: `start_lab.sh` から起動された場合（`VFPGA_INTERACTIVE=1` が有効）、`/dev/ttyPS2` 上でメニュー選択形式（1: Mount、2: Write、3: Verify、4: Umount、5: Exit）を受け付け、UARTコンソールを介して1ステップずつ任意のタイミングでテストをトリガーできます。

### 5. 【クリーンアップ処理の自動化】
- **[run_tests.sh](file:///workspaces/FPGA-BoardlessBench/tests/run_tests.sh)**
  - クリーン処理ルーチンの中に `rm -rf tests/scenarios/*/sd_card` を追加し、テスト実行時の動的生成フォルダがGitで汚染されるのを完全に防ぎます。

---

## 追加作業：Zynq デバイスファイル名およびアドレス適合化リファクタリング

実機 Zynq-7000 SoC 仕様および標準 Linux プローブ挙動に合わせたリファクタリングを実施しました。F-BBのDTS駆動型 Shim 生成のおかげで、既存のシミュレーションロジックの破壊なく完全に移行が完了しています。

### 変更箇所と設計意図

#### 1. I2Cバスの適合 (Phase 1)
* **[02_multi_i2c/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02_multi_i2c/config.dts) / [main.c](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02_multi_i2c/main.c)**
  * Zynq 内蔵 I2C アドレスに合わせてバスIDを修正。
  * `0xe0004000` (I2C0) ＝ `/dev/i2c-0`
  * `0xe0005000` (I2C1) ＝ `/dev/i2c-1`
* **[02d_oled_i2c/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02d_oled_i2c/config.dts) / [main.cpp](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02d_oled_i2c/main.cpp)**
  * `0xe0004000` にマウントされた OLED のため、オープン先を `/dev/i2c-0` に統一。

#### 2. UARTの適合 (Phase 2)
* **[02b_multi_spi/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02b_multi_spi/config.dts) / [main.cpp](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02b_multi_spi/main.cpp)**
* **[03_uart_console/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/03_uart_console/config.dts) / [main.c](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/03_uart_console/main.c)**
* **[19_acore_sd/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/19_acore_sd/config.dts) / [main.c](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/19_acore_sd/main.c)**
  * `0xe0001000` (Zynq PS UART1 領域) に配置されているシリアルコントローラノードについて、互換名を Zynq PS UART 標準の `xlnx,xuartps` に適合させ、デバイス名を `/dev/ttyPS1` に変更。
* **[02c_pl_spi/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02c_pl_spi/config.dts) / [main.cpp](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/02c_pl_spi/main.cpp)**
  * PL側に接続されている UART Lite のため、アドレスを PL 領域 `0x40003000` に移動。互換名は `xlnx,xps-uartlite-1.00.a` のまま、デバイス名を標準通り `/dev/ttyUL0` に変更。
* **[08_multi_uart/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/08_multi_uart/config.dts) / [main.c](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/08_multi_uart/main.c)**
  * 最大2ポートのPS UART制限を守るため、3番目のシリアル `uart3` を PL 領域のアドレス `0x40003000` に移動し、デバイス名を `/dev/ttyUL0` に変更。

#### 3. UIOプローブ順序の適合 (Phase 3)
* **[S01_cpp_lfsr_sequencer/config.dts](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/S01_cpp_lfsr_sequencer/config.dts) / [main.cpp](file:///workspaces/FPGA-BoardlessBench/tests/scenarios/S01_cpp_lfsr_sequencer/main.cpp)**
  * アドレス順のプローブ規則に従い、低アドレス `0x41200000` (Gpio) を `/dev/uio0` に、高アドレス `0x42000000` (Custom) を `/dev/uio1` にマッピング順序を入れ替え。

---

## デバッグと検証

### C-ShimでのI2Cバスインデックス判定バグ修正
リファクタリング後にシナリオ `02_multi_i2c` で `/dev/i2c-0` の `open` が失敗するバグが発生したため、以下のデバッグと修正を行いました。

1. **`is_i2c_device` が返す値の調整**:
   - `open`/`open64`/`openat`/`openat64` フックで `is_i2c_device` の戻り値が `0`（インデックス0＝偽値）の場合にフックがスキップされる不具合がありました。
   - `generator_shim.py` を修正し、マッチした場合は `bus_id + 1` (非ゼロ値) を返すように変更しました。
   - `libfpgashim.c.template` 内で `int i2c_bus_id = i2c_code - 1;` としてインデックスを戻してフックマップに登録するよう修正しました。

2. **`ioctl` フックの範囲判定条件修正**:
   - `bus_id = 0` の場合、`virtual_fd_route_idx[fd]` が `-100` に設定されます。
   - `ioctl` フックにおけるI2Cデバイス判定の閾値が `virtual_fd_route_idx[fd] <= -101` となっていたため、`-100` が除外され、実 `/dev/null` への `ioctl` が実行されて `Inappropriate ioctl for device` エラーが発生していました。
   - これを `virtual_fd_route_idx[fd] <= -100` に修正し、バス0のI2Cフックが正常に行われるようにしました。

### 自動回帰テスト実行結果 (run_tests.sh)
修正適用後、全自動テストスイートをクリーンビルドの状態で実行し、**すべてのシナリオテストが正常にパス (PASSED) することを確認しました。**
```
[Runner] RESULT: 13_amp_mcore_cmsis-rtos2-threadx PASSED
[Runner] RESULT: 14_amp_mcore_OpenAMP_baremetal PASSED
[Runner] RESULT: 15_amp_mcore_OpenAMP_freertos PASSED
[Runner] RESULT: 16_amp_mcore_Rust_baremetal PASSED
[Runner] RESULT: 17_amp_mcore_Rust_embassy PASSED
[Runner] RESULT: 18_amp_mcore_Rust_rtic PASSED

[Runner] ALL SCENARIOS COMPLETED SUCCESSFULLY!
```
また、シナリオ `02_multi_i2c` を個別実行した結果、両方のI2Cバスが正しくフックされ、期待データが読み出されることを実証しました。
```
--- Multi-I2C Test Start ---
[Shim Debug] open: /dev/i2c-0
[App] Bus 1 (0x50) returned: 0x10
[Shim Debug] open: /dev/i2c-1
[App] Bus 2 (0x36) returned: 0x20
[App] SUCCESS: Multiple I2C buses identified correctly!
--- Multi-I2C Test End ---

[Runner] RESULT: SUCCESS
```
これにより、FW アプリケーションの物理アドレス空間とデバイス命名の不整合が解消され、**F-BB 環境下でのシミュレーション互換性を保ったまま実機移植性（透過性）が極めて高い状態**にブラッシュアップされました。
