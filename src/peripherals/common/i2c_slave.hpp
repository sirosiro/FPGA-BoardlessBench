#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

/**
 * @brief 仮想I2Cスレーブデバイスの抽象基底クラス
 * 
 * UNIXドメインソケットとの通信制御、ヘッダー解析、
 * およびメインの待ち受けループ処理を提供します。
 */
class I2cSlave {
public:
    /**
     * @brief コンストラクタ
     * @param dev_addr I2Cデバイスアドレス
     */
    I2cSlave(uint8_t dev_addr);

    /**
     * @brief デストラクタ (RAIIによる自動リソース回収)
     */
    virtual ~I2cSlave();

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
     * @brief I2C書き込みメッセージ受信時のイベントハンドラ (派生クラスで実装)
     * @param data 受信したデータバイト列
     */
    virtual void onWrite(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief I2C読み出しメッセージ受信時のイベントハンドラ (派生クラスで実装)
     * @param length 読み出す要求バイト長
     * @return スレーブデバイスが応答するデータバイト列
     */
    virtual std::vector<uint8_t> onRead(size_t length) = 0;

    uint8_t dev_addr_;              ///< I2C 7-bit アドレス
    int server_fd_{-1};             ///< サーバー側(待ち受け)ソケットディスクリプタ
    std::string socket_path_;       ///< ソケットファイルのパス
    std::atomic<bool> running_{false}; ///< ループ制御フラグ

private:
    /**
     * @brief クライアント接続とのI2C同期通信処理ループ
     * @param client_fd 接続されたクライアントのソケット
     */
    void handleClient(int client_fd);
};
