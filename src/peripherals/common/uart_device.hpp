#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

/**
 * @brief 仮想UARTデバイスの抽象基底クラス
 * 
 * PTY (Pseudo-Terminal) スレーブデバイス名のポーリング読み出し、
 * スレーブのオープン、および無限ループでの受信監視を提供します。
 */
class UartDevice {
public:
    /**
     * @brief コンストラクタ
     */
    UartDevice();

    /**
     * @brief デストラクタ (RAIIによる自動リソース回収)
     */
    virtual ~UartDevice();

    /**
     * @brief UARTエミュレーションの起動と監視イベントループ開始 (ブロッキング)
     * @param pty_map_path PTYスレーブパスが書き込まれるマップファイルパス
     * @return 起動に成功した場合は true, 失敗した場合は false
     */
    bool start(const std::string& pty_map_path);

    /**
     * @brief エミュレーションの停止
     */
    void stop();

protected:
    /**
     * @brief PTYからデータを受信した際のイベントハンドラ (派生クラスで実装)
     * @param data 受信したデータバイト列
     */
    virtual void onReceive(const std::vector<uint8_t>& data) = 0;

    /**
     * @brief クライアント(PTY経由)へデータを送信する
     * @param data 送信するデータバイト列
     */
    void transmit(const std::vector<uint8_t>& data);

    int pty_fd_{-1};                ///< PTYスレーブのファイルディスクリプタ
    std::string pty_map_path_;      ///< PTYスレーブパスが格納されたファイルのパス
    std::atomic<bool> running_{false}; ///< ループ制御フラグ

private:
    /**
     * @brief PTYからのポーリング・受信イベントループ
     */
    void eventLoop();
};
