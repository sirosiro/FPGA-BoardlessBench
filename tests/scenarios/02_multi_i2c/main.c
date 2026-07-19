#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <string.h>

#include "vfpga_device_config.h"

/**
 * 【解説: I2C通信関数】
 * Linuxでは、I2Cデバイスへのアクセスは標準的なファイル操作(open/ioctl)として抽象化されています。
 */
int read_from_bus(const char* path, unsigned char slave_addr) {
    // 【解説】 指定されたI2Cバス（例: /dev/i2c-0）をオープンします。
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    unsigned char buf[1] = {0};
    struct i2c_msg msgs[1];
    struct i2c_rdwr_ioctl_data msgset[1];

    // 【解説: i2c_msg 構造体】
    // 1回の通信（メッセージ）の構成を定義します。
    msgs[0].addr = slave_addr;    // 通信対象のI2Cスレーブアドレス
    msgs[0].flags = I2C_M_RD;     // 通信方向のフラグ（I2C_M_RD は読み出し）
    msgs[0].len = 1;              // 読み出しバイト数
    msgs[0].buf = buf;            // データを格納するバッファへのポインタ

    // 【解説: i2c_rdwr_ioctl_data 構造体】
    // 複数の i2c_msg をまとめて実行するためのセットです。
    msgset[0].msgs = msgs;
    msgset[0].nmsgs = 1;          // メッセージの数

    // 【解説: I2C_RDWR ioctl】
    // LinuxカーネルのI2Cドライバに対して、定義したメッセージセットの実行を依頼します。
    // read/writeシステムコールを何度も呼ぶより効率的で、一連の通信を安全に実行できます。
    if (ioctl(fd, I2C_RDWR, &msgset) < 0) {
        perror("ioctl");
        close(fd);
        return -1;
    }

    close(fd);
    return buf[0];
}

int main() {
    printf("--- Multi-I2C Test Start ---\n");

    // 【解説】
    // 同一の slave_addr (0x50 と 0x36) に対して、異なるバスパス（/dev/i2c-0 と /dev/i2c-1）
    // を通じてアクセスすることで、物理的に異なる回路に接続されたデバイスを操作しています。
    
    int val1 = read_from_bus(FBB_DEV_PATH_I2C_0, 0x50);
    printf("[App] Bus 1 (0x50) returned: 0x%02X\n", val1);

    int val2 = read_from_bus(FBB_DEV_PATH_I2C_1, 0x36);
    printf("[App] Bus 2 (0x36) returned: 0x%02X\n", val2);

    if (val1 == 0x10 && val2 == 0x20) {
        printf("[App] SUCCESS: Multiple I2C buses identified correctly!\n");
    } else {
        printf("[App] FAILURE: Unexpected data (Expected 0x10 and 0x20)\n");
    }

    printf("--- Multi-I2C Test End ---\n");
    return 0;
}
