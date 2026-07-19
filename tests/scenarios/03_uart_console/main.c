#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>

#include "vfpga_device_config.h"
/**
 * 【解説: UART(シリアルポート)の制御】
 * FPGA上のUARTは、Linuxからは「TTYデバイス」というキャラクターデバイスの一種として見えます。
 * 特殊なライブラリは不要で、標準的なファイル操作APIで読み書きできます。
 */
int main() {
    printf("--- UART Access Test Start ---\n");

    // 【解説: デバイスのオープン】
    // /dev/ttyPS1 をオープンします。
    // O_NOCTTY は、「このデバイスをプロセスの制御端末（Control Terminal）にしない」という
    // シリアルポートを開く際の標準的なお作法です。
    printf("[App] Opening /dev/ttyPS1...\n");
    int fd = open(FBB_DEV_PATH_SERIAL, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        printf("[App] FAILURE: Could not open /dev/ttyPS1\n");
        return 1;
    }

    printf("[App] Successfully opened /dev/ttyPS1\n");

    // 【解説: Rawモードの設定】
    // シリアルポートのエコーバックや特殊文字処理を無効化し、
    // 生のデータをそのままやり取りできるように設定します。
    struct termios options;
    tcgetattr(fd, &options);
    cfmakeraw(&options);
    tcsetattr(fd, TCSANOW, &options);

    // 【解説: データの送信】
    const char *msg = "\r\n"
                      "========================================\r\n"
                      "   F-BB UART Console Shell\r\n"
                      "========================================\r\n"
                      "Type 'hello' or 'exit'.\r\n"
                      "\r\n"
                      "Login: ";
    write(fd, msg, strlen(msg));

    // 【解説: 実行モードの切り替え】
    // 環境変数 VFPGA_INTERACTIVE が設定されている場合のみ、対話ループに入ります。
    // これにより、自動テスト（run_tests.sh）を妨げずにダッシュボードでのデモを両立させます。
    if (getenv("VFPGA_INTERACTIVE")) {
        printf("[App] Entering Interactive Mode (VFPGA_INTERACTIVE is set)\n");
        char buf[128];
        while (1) {
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                if (strstr(buf, "hello")) {
                    const char *reply = "Hi there! This is FPGA hardware IP speaking.\r\n> ";
                    write(fd, reply, strlen(reply));
                } else if (strstr(buf, "exit")) {
                    const char *reply = "Goodbye!\r\n";
                    write(fd, reply, strlen(reply));
                    break;
                } else {
                    write(fd, "> ", 2);
                    write(fd, buf, n);
                    write(fd, "\r\n> ", 4);
                }
            }
            usleep(100000); // 100ms
        }
    } else {
        printf("[App] Running in Automated Mode. Greeting sent.\n");
    }

    close(fd);
    printf("--- UART Access Test End ---\n");
    return 0;
}
