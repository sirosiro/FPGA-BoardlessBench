#include <iostream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <cstring>
#include <algorithm>
#include <string>

#include "vfpga_device_config.h"

constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t SEG7_ADDR = 0x70;

// Standard 7-Segment Font Encoding (A=bit0, B=bit1, C=bit2, D=bit3, E=bit4, F=bit5, G=bit6, DP=bit7)
static const uint8_t SEVEN_SEG_FONT[16] = {
    0x3F, // '0'
    0x06, // '1'
    0x5B, // '2'
    0x4F, // '3'
    0x66, // '4'
    0x6D, // '5'
    0x7D, // '6'
    0x07, // '7'
    0x7F, // '8'
    0x6F, // '9'
    0x77, // 'A'
    0x7C, // 'b'
    0x39, // 'C'
    0x5E, // 'd'
    0x79, // 'E'
    0x71  // 'F'
};

// I2C_RDWR を使用してデータを送信する関数
void write_i2c_data(int fd, uint8_t slave_addr, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> writable_data = data;
    struct i2c_msg msg;
    msg.addr = slave_addr;
    msg.flags = 0; // 書き込み
    msg.len = writable_data.size();
    msg.buf = writable_data.data();

    struct i2c_rdwr_ioctl_data msgset;
    msgset.msgs = &msg;
    msgset.nmsgs = 1;

    ioctl(fd, I2C_RDWR, &msgset);
}

void oled_write_cmds(int fd, const std::vector<uint8_t>& cmds) {
    std::vector<uint8_t> buf;
    buf.push_back(0x00);
    buf.insert(buf.end(), cmds.begin(), cmds.end());
    write_i2c_data(fd, OLED_ADDR, buf);
    usleep(1000);
}

void oled_write_data(int fd, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> buf;
    buf.push_back(0x40);
    buf.insert(buf.end(), data.begin(), data.end());
    write_i2c_data(fd, OLED_ADDR, buf);
    usleep(1000);
}

void oled_set_pixel(std::vector<uint8_t>& buf, int x, int y, bool on = true) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    int page = y / 8;
    int bit = y % 8;
    int addr = page * 128 + x;
    if (on) buf[addr] |= (1 << bit);
    else buf[addr] &= ~(1 << bit);
}

void oled_draw_char(std::vector<uint8_t>& buf, char c, int x, int y) {
    static std::vector<std::vector<uint8_t>> font_table(128, std::vector<uint8_t>(8, 0));
    static bool initialized = false;
    if (!initialized) {
        font_table[' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        font_table['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
        font_table[':'] = {0x00, 0x0c, 0x0c, 0x00, 0x00, 0x0c, 0x0c, 0x00};
        font_table['-'] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00};
        font_table['0'] = {0x3e, 0x51, 0x49, 0x45, 0x3e, 0x00, 0x00, 0x00};
        font_table['1'] = {0x00, 0x42, 0x7f, 0x40, 0x00, 0x00, 0x00, 0x00};
        font_table['2'] = {0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00, 0x00};
        font_table['3'] = {0x21, 0x41, 0x45, 0x4b, 0x31, 0x00, 0x00, 0x00};
        font_table['4'] = {0x18, 0x14, 0x12, 0x7f, 0x10, 0x00, 0x00, 0x00};
        font_table['5'] = {0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00, 0x00};
        font_table['6'] = {0x3c, 0x4a, 0x49, 0x49, 0x30, 0x00, 0x00, 0x00};
        font_table['7'] = {0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00, 0x00};
        font_table['8'] = {0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00};
        font_table['9'] = {0x06, 0x49, 0x49, 0x29, 0x1e, 0x00, 0x00, 0x00};
        font_table['F'] = {0x7f, 0x09, 0x09, 0x09, 0x01, 0x00, 0x00, 0x00};
        font_table['B'] = {0x7f, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00};
        font_table['I'] = {0x00, 0x41, 0x7f, 0x41, 0x00, 0x00, 0x00, 0x00};
        font_table['o'] = {0x38, 0x44, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00};
        font_table['T'] = {0x01, 0x01, 0x7f, 0x01, 0x01, 0x00, 0x00, 0x00};
        font_table['S'] = {0x26, 0x49, 0x49, 0x49, 0x32, 0x00, 0x00, 0x00};
        font_table['A'] = {0x7e, 0x09, 0x09, 0x09, 0x7e, 0x00, 0x00, 0x00};
        font_table['O'] = {0x3e, 0x41, 0x41, 0x41, 0x3e, 0x00, 0x00, 0x00};
        font_table['N'] = {0x7f, 0x04, 0x08, 0x10, 0x7f, 0x00, 0x00, 0x00};
        font_table['D'] = {0x7f, 0x41, 0x41, 0x22, 0x1c, 0x00, 0x00, 0x00};
        font_table['E'] = {0x7f, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00, 0x00};
        font_table['M'] = {0x7f, 0x02, 0x04, 0x02, 0x7f, 0x00, 0x00, 0x00};
        font_table['P'] = {0x7f, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00};
        font_table['x'] = {0x41, 0x22, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00};
        initialized = true;
    }
    const auto& glyph = font_table[static_cast<uint8_t>(c)];
    for (int col = 0; col < 8; ++col) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 8; ++row) {
            if (line & (1 << row)) oled_set_pixel(buf, x + col, y + row);
        }
    }
}

void oled_draw_string(std::vector<uint8_t>& buf, const std::string& str, int x, int y) {
    int cur_x = x;
    for (char c : str) {
        oled_draw_char(buf, c, cur_x, y);
        cur_x += 7;
    }
}

// 7-Seg LED RAM更新関数 (Digit0, Digit1, Colon, Digit2, Digit3)
void seg7_write_display(int fd, uint8_t d0, uint8_t d1, bool colon, uint8_t d2, uint8_t d3) {
    std::vector<uint8_t> ram(10, 0);
    ram[0] = 0x00; // RAM Start Address
    ram[1] = d0;   // Digit 0
    ram[3] = d1;   // Digit 1
    ram[5] = colon ? 0x02 : 0x00; // Colon (bit 1)
    ram[7] = d2;   // Digit 2
    ram[9] = d3;   // Digit 3
    write_i2c_data(fd, SEG7_ADDR, ram);
}

int main() {
    std::cout << "[02e Scenario] Opening I2C device (/dev/i2c-0)...\n";
    int fd = open(FBB_DEV_PATH_I2C, O_RDWR);
    if (fd < 0) {
        std::cerr << "Failed to open " FBB_DEV_PATH_I2C "\n";
        return 1;
    }

    std::cout << "[02e Scenario] Waiting for periphearl daemons (OLED & HT16K33)...\n";
    sleep(1);

    // HT16K33 7セグLED初期化
    write_i2c_data(fd, SEG7_ADDR, {0x21}); // Oscillator ON
    write_i2c_data(fd, SEG7_ADDR, {0x81}); // Display ON, Blink OFF
    write_i2c_data(fd, SEG7_ADDR, {0xEF}); // Brightness Max

    // SSD1306 OLED初期化
    oled_write_cmds(fd, {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    });

    std::cout << "[02e Scenario] Multi-peripheral initialization complete!\n";

    int sec_counter = 0;
    while (true) {
        // --- モード 1: クロック・タイマーモード (MM:SS) ---
        for (int step = 0; step < 10; ++step) {
            int minutes = sec_counter / 60;
            int seconds = sec_counter % 60;
            bool colon = (step % 2 == 0); // 1Hz明滅

            // 7セグLED更新
            seg7_write_display(fd,
                SEVEN_SEG_FONT[(minutes / 10) % 10],
                SEVEN_SEG_FONT[minutes % 10],
                colon,
                SEVEN_SEG_FONT[(seconds / 10) % 10],
                SEVEN_SEG_FONT[seconds % 10]
            );

            // OLED UI更新
            std::vector<uint8_t> oled_buf(1024, 0);
            oled_draw_string(oled_buf, "F-BB IOT STATION", 8, 4);
            oled_draw_string(oled_buf, "MODE: RUNNING", 8, 20);
            
            char time_str[32];
            snprintf(time_str, sizeof(time_str), "TIME: %02d:%02d", minutes, seconds);
            oled_draw_string(oled_buf, time_str, 8, 36);

            // プログレスバー描画
            for (int x = 8; x < 120; ++x) {
                oled_set_pixel(oled_buf, x, 54);
                oled_set_pixel(oled_buf, x, 58);
            }
            int fill_w = (sec_counter % 20) * 5 + 8;
            for (int x = 8; x < std::min(fill_w, 119); ++x) {
                for (int y = 55; y <= 57; ++y) oled_set_pixel(oled_buf, x, y);
            }

            oled_write_cmds(fd, {0x20, 0x00, 0x21, 0, 127, 0x22, 0, 7});
            oled_write_data(fd, oled_buf);

            sec_counter++;
            usleep(500000); // 500ms
        }

        // --- モード 2: システム診断モード (Hex点滅 & 診断UI) ---
        std::cout << "[02e Scenario] Entering Diagnostic Mode (Hex Status & HT16K33 Blinking)...\n";
        
        // 7セグLED: 1Hz点滅 (0x85) で "dEAd"
        write_i2c_data(fd, SEG7_ADDR, {0x85});
        seg7_write_display(fd,
            SEVEN_SEG_FONT[0x0D], // 'd'
            SEVEN_SEG_FONT[0x0E], // 'E'
            false,
            SEVEN_SEG_FONT[0x0A], // 'A'
            SEVEN_SEG_FONT[0x0D]  // 'd'
        );

        for (int i = 0; i < 4; ++i) {
            std::vector<uint8_t> oled_buf(1024, 0);
            oled_draw_string(oled_buf, "F-BB DIAGNOSTIC", 8, 4);
            oled_draw_string(oled_buf, "STATUS: 0xDEAD", 8, 24);
            oled_draw_string(oled_buf, "TEST: PASSED", 8, 40);
            oled_write_cmds(fd, {0x20, 0x00, 0x21, 0, 127, 0x22, 0, 7});
            oled_write_data(fd, oled_buf);
            usleep(500000);
        }

        // 7セグLED: 点滅オフ (0x81) に戻す
        write_i2c_data(fd, SEG7_ADDR, {0x81});
    }

    close(fd);
    return 0;
}
