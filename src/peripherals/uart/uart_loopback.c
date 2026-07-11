#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>

static int pty_fd = -1;

void cleanup(void) {
    if (pty_fd != -1) {
        close(pty_fd);
    }
}

void handle_signal(int sig) {
    (void)sig;
    cleanup();
    exit(0);
}

int main(int argc, char *argv[]) {
    char *pts_file = NULL;
    char pts_name[256] = "";

    // 引数解析
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pts-file") == 0 && i + 1 < argc) {
            pts_file = argv[++i];
        }
    }

    if (!pts_file) {
        fprintf(stderr, "Usage: %s --pts-file <path_to_vfpga_uart_X_file>\n", argv[0]);
        return 1;
    }

    // PTYスレーブデバイス名がファイルに書き出されるのをポーリング待機
    printf("[UART Loopback] Waiting for PTY name in %s...\n", pts_file);
    fflush(stdout);
    for (int retry = 0; retry < 30; retry++) {
        FILE *f = fopen(pts_file, "r");
        if (f) {
            if (fgets(pts_name, sizeof(pts_name), f) != NULL) {
                // 改行文字のトリム
                pts_name[strcspn(pts_name, "\r\n")] = '\0';
                fclose(f);
                break;
            }
            fclose(f);
        }
        usleep(500000); // 500ms待機
    }

    if (strlen(pts_name) == 0) {
        fprintf(stderr, "[UART Loopback] Error: Failed to read PTY name from %s\n", pts_file);
        return 1;
    }

    printf("[UART Loopback] Opening PTY slave device: %s\n", pts_name);
    fflush(stdout);

    // ブロッキングモードでPTYスレーブをオープン
    pty_fd = open(pts_name, O_RDWR | O_NOCTTY);
    if (pty_fd == -1) {
        perror("open PTY slave");
        return 1;
    }

    // クリーンアップシグナル登録
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[UART Loopback] Loopback daemon started. Waiting for data...\n");
    fflush(stdout);

    uint8_t buf[256];
    while (1) {
        ssize_t bytes_read = read(pty_fd, buf, sizeof(buf));
        if (bytes_read <= 0) {
            break;
        }

        // 受信したデータをそのまま同じスレーブFDへ書き込んで送り返す（ループバック）
        ssize_t bytes_written = write(pty_fd, buf, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write PTY");
            break;
        }
    }

    cleanup();
    return 0;
}
