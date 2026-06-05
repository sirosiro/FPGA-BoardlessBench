# GPU画像処理HAL実装ステータス (GPU Image Processing HAL Implementation Status)

## 概要
現在、`IVideoProcessor` インタフェースの実装である `ImxGlesProcessorBase` (およびその派生クラスである `Imx8mpGlesProcessor`, `Imx95GlesProcessor`) は、**実機ハードウェア（Production）パスがダミー（モック）実装**となっています。
一方、シミュレーション（ホスト）環境用の `HostGlesProcessor` には、GLSLシェーダを用いた本物の歪み補正・合成（スティッチング）のロジックが実装されています。

---

## 1. 現状の実装状況

| クラス名 | ターゲット環境 | 実装ステータス | 詳細 |
| :--- | :--- | :--- | :--- |
| `HostGlesProcessor` | ホスト（Mesa/llvmpipe） | **完全実装 (Production-ready simulation)** | サーフェスレスEGL、GLSLによる4画像合成および歪み補正をエミュレート。 |
| `Imx8mpGlesProcessor` | i.MX 8M Plus 実機 | **ダミー実装 (Mock/Dummy)** | EGLコンテキスト初期化スケルトンのみ。`processFrame` は単なるパススルー（メモリコピー）。 |
| `Imx95GlesProcessor` | i.MX 95 実機 | **ダミー実装 (Mock/Dummy)** | 同上。 |

---

## 2. 実機パスがダミーである理由

### ① 実機依存ヘッダ・ライブラリの不在
実機におけるグラフィックスバッファ連携（ゼロコピー）には、Linux標準の `GBM`（Generic Buffer Manager）や `libdrm`、またNXP SoC独自のGPUドライバ（Vivante / Mali用のドライバライブラリ）が必要ですが、現在のF-BBコンテナ環境（ホスト）にはこれらがインストールされておらず、ビルドが不可能です。

### ② 実行時ハードウェアリソースの不在
仮にビルドを通したとしても、シミュレータホスト上には `/dev/dri/card0` やGPU物理メモリ、専用カーネルドライバといったハードウェアリソースが存在しないため、バッファ確保やGPUバインド処理を実行した瞬間に強制終了（クラッシュ）します。

### ③ 実機デバッグの必要性
GPUメモリ（DMA-BUF等）をゼロコピーで同期・マッピングするコードは、アライメントの不整合やキャッシュの同期ミスによって、容易にOSカーネルのフリーズやセグメンテーションフォールトを引き起こすため、実機ターゲット（およびデバッグログ `dmesg` 等）なしで安全に書き上げることは極めて困難です。

---

## 3. 今後の実装計画 (ロードマップ)

実機へのデプロイおよび動作確認を行うフェーズにおいて、以下の手順で Production パスを実装します。

### Step 1: インターフェースの拡張
現在のCPUバッファベース（`const uint8_t* in_frames[4]`）から、実機用のDMA-BUFファイル記述子（`int dmabuf_fds[4]` 等）やバッファ構造体を直接渡せるよう、`IVideoProcessor::processFrame` のシグネチャを拡張するか、別メソッドを追加します。

### Step 2: 実機SDK環境への移行
NXPのYoctoベースSDK（あるいは対応する実機クロスコンパイル環境）でビルドを実行し、`<gbm.h>` や `<EGL/eglext.h>` の実機独自拡張が正しくビルドされるようにします。

### Step 3: GBMおよびEGL Imageをベースとしたゼロコピー描画の実装
`processFrame` 内で、受け取った `dmabuf fd` から `eglCreateImageKHR` を使ってEGLImageを作成し、これを `glEGLImageTargetTexture2DOES` を介してテクスチャとしてバインドし、GPU側でGLSLシェーダ（`HostGlesProcessor` から移植したもの）を実行してゼロコピーで超高速処理を行うロジックを実装します。

### Step 4: 実機上でのメモリバリアと同期処理のデバッグ
CPU/GPU間のアクセスレースを防ぐためのフェンス制御や、メモリキャッシュの一貫性（キャッシュフラッシュ）処理を追加し、実機ハードウェア上での安定動作を検証します。
