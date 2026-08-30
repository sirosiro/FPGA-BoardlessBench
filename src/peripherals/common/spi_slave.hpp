/**
 * @file src/peripherals/common/spi_slave.hpp
 * @intent:responsibility
 *   UNIX ドメインソケット上で SPI プロトコルメッセージ（2バイト長ヘッダー + 全二重データ列）を
 *   同期送受信する仮想 SPI スレーブデバイスの抽象基底クラス（SpiSlave）を定義する。
 * @intent:rationale
 *   SPI の全二重通信（マスタ送信 tx_data と同時にスレーブ応答 rx_data を返す契約）をプロセス間ソケットで
 *   高精度にモデル化し、ADC, Flash メモリ等の SPI デバイスを独立プロセスとして動作可能にする。
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

/**
 * @class SpiSlave
 * @intent:responsibility
 *   UNIX ドメインソケットの待ち受け、SPI 全二重パケット（tx/rx）の同期処理、
 *   および派生クラスの onTransfer ハンドラへのディスパッチを担当。
 * @intent:pre-condition
 *   派生クラスは onTransfer() を実装し、tx_data と同サイズの応答バイト列を返却すること。
 */
class SpiSlave {
public:
    /**
     * @brief コンストラクタ
     * @param cs チップセレクトインデックス (通常は0または1)
     * @intent:responsibility デバイスのチップセレクト番号を保持する。
     */
    SpiSlave(uint8_t cs);

    /**
     * @brief デストラクタ (RAIIによる自動リソース回収)
     * @intent:responsibility ソケットのクローズとソケットファイルの削除を保証する。
     */
    virtual ~SpiSlave();

    /**
     * @brief エミュレーションデーモンの起動とイベントループ開始 (ブロッキング)
     * @param socket_path UNIXドメインソケットファイルパス
     * @return 起動に成功した場合は true, 失敗した場合は false
     * @intent:responsibility ソケットの bind/listen を行い、クライアント全二重通信の処理ループを開始する。
     */
    bool start(const std::string& socket_path);

    /**
     * @brief エミュレーションの停止
     * @intent:responsibility running_ フラグを false にし、ソケットリソースを解放する。
     */
    void stop();

protected:
    /**
     * @brief SPIデータ全二重転送時のイベントハンドラ (派生クラスで実装)
     * @param tx_data マスタから送信されたデータバイト列
     * @return スレーブからマスタへ同時に返送するデータバイト列 (tx_dataと同じサイズである必要があります)
     * @intent:responsibility コマンド解析（Read/Write/Status等）を行い、同期応答バイト列を生成する。
     */
    virtual std::vector<uint8_t> onTransfer(const std::vector<uint8_t>& tx_data) = 0;

    uint8_t cs_;                    ///< チップセレクト番号
    int server_fd_{-1};             ///< サーバー側(待ち受け)ソケットディスクリプタ
    std::string socket_path_;       ///< ソケットファイルのパス
    std::atomic<bool> running_{false}; ///< ループ制御フラグ

private:
    /**
     * @brief クライアント接続とのSPI同期通信処理ループ
     * @param client_fd 接続されたクライアントのソケット
     * @intent:responsibility 単一トランザクションの 2 バイト長ヘッダーを受信し、tx_data を受け取って onTransfer の応答を返送する。
     */
    void handleClient(int client_fd);
};
