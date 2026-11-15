# Host <-> Docker Container Shared Directory (`shared/`)

このディレクトリは、ホストマシン（Mac / Windows / Linux）と Docker コンテナ（F-BB 実行環境）の間で**直接双方向同期（バインドマウント）**されています。

---

### 使い方 (Usage)

#### 1. ホスト側（Mac の Finder など）からコンテナへファイルを渡す
* `docker cp` コマンドを使う必要はありません。
* Mac の **Finder** や **VS Code のエクスプローラー** から、この `shared/` フォルダへファイル（`.asm`, `.hex`, `.bin`, `.v`, テストデータ等）を**ドラッグ＆ドロップ**するだけで、即座にコンテナ内へ反映されます。

#### 2. コンテナ（fbb-cli）から直接使用する
コンテナ内からは `/workspaces/FPGA-BoardlessBench/shared/<ファイル名>` として直接参照できます：

```bash
# 例: shared に置いた hex ファイルを RCT-Z80 で実行
fbb lab rct-z80/scenario shared/my_custom_program.hex

# 例: shared に置いた Verilog 回路から config.gtr を自動生成
fbb import shared/my_core.v -o tests/scenarios/my_test
```

#### 3. Git 管理について
このディレクトリ内の作業ファイルは `.gitignore` により自動的に Git 除外されるため、自由にファイルの一時置き場・受け渡し場所としてご利用いただけます。

