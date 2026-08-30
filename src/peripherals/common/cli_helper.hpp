/**
 * @file src/peripherals/common/cli_helper.hpp
 * @intent:responsibility
 *   F-BB PPA 5.1 準拠ペリフェラルエミュレータプラグイン共通のコマンドライン引数パーサー（fbb::PluginCLI）を提供する。
 * @intent:rationale
 *   各ペリフェラルデーモン（I2C, SPI, UART, ディスプレイ等）でバラバラになりがちだった引数解析コードを統一し、
 *   --socket, --shm, --file, --pts-file, --init-val, --width, --height, -h/--help を一律サポートする。
 *   同時に、既存シナリオランナーや位置引数との 100% 後方互換性を担保する。
 */

#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <functional>

namespace fbb {

/**
 * @class PluginCLI
 * @intent:responsibility
 *   ペリフェラルプラグインの argc/argv をパースし、設定値の抽出、--help メニュー出力、シグナル制御を行う。
 */
class PluginCLI {
public:
    struct Options {
        std::string socket_path;
        std::string shm_path;
        std::string file_path;
        std::string pts_file;
        uint32_t init_val{0};
        bool has_init_val{false};
        int width{64};
        int height{64};
        bool show_help{false};
    };

    /**
     * @brief コンストラクタ
     * @param plugin_name プラグインの表示名（例: "Solomon SSD1306 OLED"）
     * @param description プラグインの説明文
     * @intent:responsibility プラグイン名と説明文を保持し、ヘルプメニュー表示の基盤を構築する。
     * @intent:rationale 各ペリフェラルの自己説明的メタデータを標準出力できるようにする。
     * @intent:pre-condition plugin_name, description は空でない文字列であること。
     */
    PluginCLI(const std::string& plugin_name, const std::string& description)
        : name_(plugin_name), desc_(description) {}

    /**
     * @brief コマンドライン引数をパースする
     * @param argc 引数の数
     * @param argv 引数配列
     * @return パース結果の Options 構造体
     * @intent:responsibility 標準 CLI オプションおよび位置引数を解析し、Options 構造体に格納する。
     * @intent:rationale 各ペリフェラルで重複するパース処理を排除し、--help メニューや引数互換性を一元管理する。
     * @intent:pre-condition argc >= 1 であり、argv[0] に実行バイナリ名が格納されていること。
     */
    Options parse(int argc, char* argv[]) {
        Options opt;
        prog_name_ = (argc > 0 && argv[0]) ? argv[0] : "fbb_plugin";

        std::vector<std::string> positional;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                opt.show_help = true;
                print_help();
                return opt;
            } else if ((arg == "-s" || arg == "--socket") && i + 1 < argc) {
                opt.socket_path = argv[++i];
            } else if ((arg == "--shm") && i + 1 < argc) {
                opt.shm_path = argv[++i];
            } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
                opt.file_path = argv[++i];
            } else if ((arg == "-p" || arg == "--pts-file") && i + 1 < argc) {
                opt.pts_file = argv[++i];
            } else if ((arg == "-i" || arg == "--init-val") && i + 1 < argc) {
                opt.init_val = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
                opt.has_init_val = true;
            } else if ((arg == "-w" || arg == "--width") && i + 1 < argc) {
                opt.width = std::atoi(argv[++i]);
            } else if ((arg == "-H" || arg == "--height") && i + 1 < argc) {
                opt.height = std::atoi(argv[++i]);
            } else if (arg.rfind("-", 0) != 0) {
                // Positional argument
                positional.push_back(arg);
            }
        }

        // Backward compatibility for positional arguments (e.g. hub75_matrix <shm_path> [width] [height])
        if (!positional.empty()) {
            if (opt.shm_path.empty() && opt.socket_path.empty()) {
                opt.shm_path = positional[0];
            }
            if (positional.size() >= 2) {
                opt.width = std::atoi(positional[1].c_str());
            }
            if (positional.size() >= 3) {
                opt.height = std::atoi(positional[2].c_str());
            }
        }

        return opt;
    }

    /**
     * @brief 整形されたヘルプメッセージを標準出力へ表示する
     * @intent:responsibility ペリフェラルの仕様、サポートオプション、使用例を整形表示する。
     * @intent:rationale サードパーティ開発者や利用者が CLI オプションを即座に確認できるようにする。
     * @intent:pre-condition なし。
     */
    void print_help() const {
        std::cout << "============================================================\n";
        std::cout << "🔌 F-BB PPA 5.1 Peripheral: " << name_ << "\n";
        std::cout << "============================================================\n";
        std::cout << desc_ << "\n\n";
        std::cout << "Usage:\n";
        std::cout << "  " << prog_name_ << " [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  -s, --socket <path>     UNIX domain socket path for I2C/SPI master bridge\n";
        std::cout << "      --shm <path>        POSIX shared memory path (e.g. /dev/shm/fbb_display_0)\n";
        std::cout << "  -f, --file <path>       Non-volatile storage backing file path\n";
        std::cout << "  -p, --pts-file <path>   Path to map file containing allocated PTY slave path\n";
        std::cout << "  -i, --init-val <val>    Initial register or ADC channel default value\n";
        std::cout << "  -w, --width <pixels>    Display / matrix width in pixels (Default: 64)\n";
        std::cout << "  -H, --height <pixels>   Display / matrix height in pixels (Default: 64)\n";
        std::cout << "  -h, --help              Show this help message and exit\n";
        std::cout << "============================================================\n";
    }

    /**
     * @brief SIGINT / SIGTERM シグナルハンドラを設定する
     * @param handler シグナル受信時に呼び出されるコールバック関数
     * @intent:responsibility SIGINT (Ctrl+C) および SIGTERM 受信時に安全な停止処理を行えるようハンドラを登録する。
     * @intent:rationale デーモンの異常終了や共有メモリ／ソケットの解放漏れ（残存プロセス問題）を未然に防ぐ。
     * @intent:pre-condition handler は有効な関数ポインタであること。
     */
    static void setup_signal_handler(void (*handler)(int)) {
        std::signal(SIGINT, handler);
        std::signal(SIGTERM, handler);
    }

private:
    std::string name_;
    std::string desc_;
    std::string prog_name_{"fbb_plugin"};
};

} // namespace fbb
