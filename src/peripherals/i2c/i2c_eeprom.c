#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <stdint.h>

#define EEPROM_SIZE 256

static int server_fd = -1;
static char socket_path[256] = "";

void cleanup(void) {
    if (server_fd != -1) {
        close(server_fd);
    }
    if (strlen(socket_path) > 0) {
        unlink(socket_path);
    }
}

void handle_signal(int sig) {
    (void)sig;
    cleanup();
    exit(0);
}

int main(int argc, char *argv[]) {
    char *sock_file = NULL;
    char *mock_file = NULL;
    uint8_t init_val = 0x10; // デフォルト初期値
    uint8_t memory[EEPROM_SIZE];
    memset(memory, 0, sizeof(memory));

    // 引数解析
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            sock_file = argv[++i];
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            mock_file = argv[++i];
        } else if (strcmp(argv[i], "--init-val") == 0 && i + 1 < argc) {
            init_val = (uint8_t)strtol(argv[++i], NULL, 0);
        }
    }

    if (!sock_file) {
        fprintf(stderr, "Usage: %s --socket <socket_path> [--file <mock_file>] [--init-val <val>]\n", argv[0]);
        return 1;
    }

    strncpy(socket_path, sock_file, sizeof(socket_path) - 1);
    memory[0] = init_val; // 初期値の書き込み

    // クリーンアップシグナル登録
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // 不揮発ファイルからの復元
    if (mock_file) {
        FILE *f = fopen(mock_file, "rb");
        if (f) {
            fread(memory, 1, sizeof(memory), f);
            fclose(f);
        }
    }

    // UNIXドメインソケット初期化
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    unlink(socket_path);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) == -1) {
        perror("listen");
        cleanup();
        return 1;
    }

    printf("[I2C EEPROM] Mock daemon started on %s (init-val: 0x%02X)\n", socket_path, init_val);
    fflush(stdout);

    uint8_t current_addr = 0;

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            continue;
        }

        while (1) {
            uint16_t i2c_addr = 0;
            uint16_t flags = 0;
            uint16_t len = 0;

            // 1. ヘッダー受信
            if (recv(client_fd, &i2c_addr, sizeof(i2c_addr), 0) <= 0) break;
            if (recv(client_fd, &flags, sizeof(flags), 0) <= 0) break;
            if (recv(client_fd, &len, sizeof(len), 0) <= 0) break;

            uint8_t *buf = malloc(len > 0 ? len : 1);
            if (!buf) break;

            if (flags & 0x0001) { // I2C_M_RD (読み出し)
                for (uint16_t i = 0; i < len; i++) {
                    buf[i] = memory[current_addr];
                    current_addr = (current_addr + 1) % EEPROM_SIZE;
                }
                send(client_fd, buf, len, 0);
            } else { // 書き込み
                if (recv(client_fd, buf, len, 0) <= 0) {
                    free(buf);
                    break;
                }
                if (len > 0) {
                    // 1バイト目はメモリアドレス指定
                    current_addr = buf[0] % EEPROM_SIZE;
                    // 2バイト目以降がある場合は書き込みデータ
                    for (uint16_t i = 1; i < len; i++) {
                        memory[current_addr] = buf[i];
                        current_addr = (current_addr + 1) % EEPROM_SIZE;
                    }
                    if (mock_file) {
                        FILE *f = fopen(mock_file, "wb");
                        if (f) {
                            fwrite(memory, 1, sizeof(memory), f);
                            fclose(f);
                        }
                    }
                }
            }
            free(buf);
        }
        close(client_fd);
    }

    cleanup();
    return 0;
}
