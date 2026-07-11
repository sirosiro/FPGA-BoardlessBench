#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

/**
 * @brief 仮想SPIスレーブデバイスの抽象基底クラス
 * 
 * UNIXドメインソケットを介した全二重通信、
 * およびメインの待ち受けループ処理を提供します。
 */
class SpiSlave {
public:
    /**
     * @brief コンストラクタ
     * @param cs チップセレクトインデックス (通常は0または1)
     */
    SpiSlave(uint8_t cs);

    /**
     * @brief デストラクタ (RAIIによる自動リソース回収)
     */
    virtual ~SpiSlave();

    /**
     * @brief エミュレーションデーモンの起動とイベントループ開始 (ブロッキング)
     * @param socket_path UNIXドメインソケットファイルパス
     * @return 起動に成功した場合は true, 失敗した場合は false
     */
    bool start(const std::string& socket_path);

    /**
     * @brief エミュレーションの停止
     */
    void stop();

protected:
    /**
     * @brief SPIデータ全二重転送時のイベントハンドラ (派生クラスで実装)
     * @param tx_data マスタから送信されたデータバイト列
     * @return スレーブからマスタへ同時に返送するデータバイト列 (tx_dataと同じサイズである必要があります)
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
     */
    void handleClient(int client_fd);
};
