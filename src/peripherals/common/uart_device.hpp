/**
 * @file src/peripherals/common/uart_device.hpp
 * @intent:responsibility
 *   擬似端末（PTY: Pseudo-Terminal）スレーブパスを介して非同期双方向 UART 通信を行う
 *   仮想 UART デバイスの抽象基底クラス（UartDevice）を定義する。
 * @intent:rationale
 *   Linux の標準 ttyPS* / ttyUSB* デバイスと PTY の 1:1 バインディングを提供し、
 *   ループバックデバイスや外部シリアル通信テストデーモンを独立プロセスとして動作可能にする。
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

/**
 * @class UartDevice
 * @intent:responsibility
 *   PTY マッピングファイルのポーリング待機、PTY スレーブのオープン、受信ループ、
 *   および transmit() によるデータ送出を担当。
 * @intent:pre-condition
 *   派生クラスは onReceive() を実装して受信バイト列を処理すること。
 */
class UartDevice {
public:
    /**
     * @brief コンストラクタ
     */
    UartDevice();

    /**
     * @brief デストラクタ (RAIIによる自動リソース回収)
     * @intent:responsibility PTY ディスクリプタの安全なクローズを保証する。
     */
    virtual ~UartDevice();

    /**
     * @brief UARTエミュレーションの起動と監視イベントループ開始 (ブロッキング)
     * @param pty_map_path PTYスレーブパスが書き込まれるマップファイルパス
     * @return 起動に成功した場合は true, 失敗した場合は false
     * @intent:responsibility コントローラが生成した PTY 名をポーリング取得し、オープンして受信ループを開始する。
     */
    bool start(const std::string& pty_map_path);

    /**
     * @brief エミュレーションの停止
     * @intent:responsibility running_ フラグを false にし、PTY ディスクリプタを閉じる。
     */
    void stop();

protected:
    /**
     * @brief PTYからデータを受信した際のイベントハンドラ (派生クラスで実装)
     * @param data 受信したデータバイト列
     * @intent:responsibility マスター（ファームウェア）から送信された UART バイト列を処理する。
     */
    virtual void onReceive(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief クライアント(PTY経由)へデータを送信する
     * @param data 送信するデータバイト列
     * @intent:responsibility PTY スレーブへバイト列を完全書き込みする。
     */
    void transmit(const std::vector<uint8_t>& data);

    int pty_fd_{-1};                ///< PTYスレーブのファイルディスクリプタ
    std::string pty_map_path_;      ///< PTYスレーブパスが格納されたファイルのパス
    std::atomic<bool> running_{false}; ///< ループ制御フラグ

private:
    /**
     * @brief PTYからのポーリング・受信イベントループ
     * @intent:responsibility ブロッキング read() で PTY を監視し、データ到着時に onReceive を呼ぶ。
     */
    void eventLoop();
};
