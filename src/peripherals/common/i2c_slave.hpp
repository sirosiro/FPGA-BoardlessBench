/**
 * @file src/peripherals/common/i2c_slave.hpp
 * @intent:responsibility
 *   UNIX ドメインソケット上で I2C プロトコルメッセージ（アドレス、フラグ、レングス、ペイロード）を
 *   同期送受信する仮想 I2C スレーブデバイスの抽象基底クラス（I2cSlave）を定義する。
 * @intent:rationale
 *   I2C バス通信を C-Shim（ioctl 横取り層）と疎結合な UNIX ソケットプロトコルで抽象化することで、
 *   OLED, 7seg, EEPROM 等のペリフェラルエミュレータを独立プロセス（デーモン）として並行実行可能にする。
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

/**
 * @class I2cSlave
 * @intent:responsibility
 *   UNIX ドメインソケットのリスニング、クライアント接続の accept、I2C メッセージヘッダーの解析、
 *   および派生クラスの onRead / onWrite イベントハンドラへのディスパッチを担当。
 * @intent:pre-condition
 *   派生クラスは onRead() と onWrite() を純粋仮想関数として実装すること。
 */
class I2cSlave {
public:
    /**
     * @brief コンストラクタ
     * @param dev_addr I2Cデバイスアドレス (7-bit)
     * @intent:responsibility デバイスのアドレス番号を保持する。
     */
    I2cSlave(uint8_t dev_addr);

    /**
     * @brief デストラクタ (RAIIによる自動リソース回収)
     * @intent:responsibility ソケットのクローズとソケットファイルの削除を保証する。
     */
    virtual ~I2cSlave();

    /**
     * @brief エミュレーションデーモンの起動とイベントループ開始 (ブロッキング)
     * @param socket_path UNIXドメインソケットファイルパス
     * @return 起動に成功した場合は true, 失敗した場合は false
     * @intent:responsibility ソケットの bind/listen を行い、クライアント接続の処理ループを開始する。
     * @intent:pre-condition socket_path の親ディレクトリが存在し、書き込み権限があること。
     */
    bool start(const std::string& socket_path);

    /**
     * @brief エミュレーションの停止
     * @intent:responsibility running_ フラグを false にし、ソケットとファイルリソースを解放する。
     */
    void stop();

protected:
    /**
     * @brief I2C書き込みメッセージ受信時のイベントハンドラ (派生クラスで実装)
     * @param data 受信したデータバイト列
     * @intent:responsibility マスターからの書き込みデータ（レジスタ設定・コマンド・描画データ等）を処理する。
     */
    virtual void onWrite(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief I2C読み出しメッセージ受信時のイベントハンドラ (派生クラスで実装)
     * @param length 読み出す要求バイト長
     * @return スレーブデバイスが応答するデータバイト列
     * @intent:responsibility マスターからの要求長に応じたレジスタ・センサデータを生成して返却する。
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
     * @intent:responsibility 接続された単一クライアントからのヘッダー（addr, flags, len）をパースし、読み書きを中継する。
     */
    void handleClient(int client_fd);
};
