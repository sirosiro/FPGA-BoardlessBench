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
// I2C_RDWR を使用してデータを送信する関数
void write_i2c_data(int fd, uint8_t slave_addr, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> writable_data = data; // ノンconstなバッファを用意
    struct i2c_msg msg;
    msg.addr = slave_addr;
    msg.flags = 0; // 書き込み
    msg.len = writable_data.size();
    msg.buf = writable_data.data();

    struct i2c_rdwr_ioctl_data msgset;
    msgset.msgs = &msg;
    msgset.nmsgs = 1;

    if (ioctl(fd, I2C_RDWR, &msgset) < 0) {
        std::cerr << "Failed to write I2C data via ioctl I2C_RDWR\n";
    }
}

void write_cmds(int fd, const std::vector<uint8_t>& cmds) {
    std::vector<uint8_t> buf;
    buf.push_back(0x00); // Control byte (Co=0, D/C#=0 -> All following bytes are commands)
    buf.insert(buf.end(), cmds.begin(), cmds.end());
    write_i2c_data(fd, 0x3C, buf);
    usleep(1000); // デーモンのソケット接続クリーンアップのための微小ウェイト
}

void write_data(int fd, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> buf;
    buf.push_back(0x40); // Control byte (Co=0, D/C#=1 -> All following bytes are data)
    buf.insert(buf.end(), data.begin(), data.end());
    write_i2c_data(fd, 0x3C, buf);
    usleep(1000); // デーモンのソケット接続クリーンアップのための微小ウェイト
}

// ピクセルを設定する補助関数 (128x64バッファのインデックスに変換)
void set_pixel(std::vector<uint8_t>& buf, int x, int y, bool on = true) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    int page = y / 8;
    int bit = y % 8;
    int addr = page * 128 + x;
    if (on) {
        buf[addr] |= (1 << bit);
    } else {
        buf[addr] &= ~(1 << bit);
    }
}

// 簡易8x8フォント文字の描画関数
void draw_char(std::vector<uint8_t>& buf, char c, int x, int y) {
    static std::vector<std::vector<uint8_t>> font_table(128, std::vector<uint8_t>(8, 0));
    static bool initialized = false;
    
    if (!initialized) {
        font_table[' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        font_table['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
        font_table['%'] = {0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00};
        font_table[':'] = {0x00, 0x0c, 0x0c, 0x00, 0x00, 0x0c, 0x0c, 0x00};
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
        font_table['T'] = {0x01, 0x01, 0x7f, 0x01, 0x01, 0x00, 0x00, 0x00};
        font_table['H'] = {0x7f, 0x08, 0x08, 0x08, 0x7f, 0x00, 0x00, 0x00};
        font_table['C'] = {0x3e, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00, 0x00};
        font_table['e'] = {0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x00, 0x00};
        font_table['m'] = {0x7c, 0x04, 0x78, 0x04, 0x78, 0x00, 0x00, 0x00};
        font_table['p'] = {0x7c, 0x14, 0x14, 0x14, 0x08, 0x00, 0x00, 0x00};
        font_table['i'] = {0x00, 0x44, 0x7d, 0x40, 0x00, 0x00, 0x00, 0x00};
        font_table['n'] = {0x7c, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00, 0x00};
        font_table['s'] = {0x48, 0x54, 0x54, 0x54, 0x20, 0x00, 0x00, 0x00};
        font_table['d'] = {0x30, 0x48, 0x48, 0x4a, 0x7f, 0x00, 0x00, 0x00};
        font_table['o'] = {0x38, 0x44, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00};
        font_table['u'] = {0x3c, 0x40, 0x40, 0x20, 0x7c, 0x00, 0x00, 0x00};
        font_table['t'] = {0x04, 0x3f, 0x44, 0x40, 0x20, 0x00, 0x00, 0x00};
        font_table['y'] = {0x0c, 0x50, 0x50, 0x50, 0x3c, 0x00, 0x00, 0x00};
        font_table['*'] = {0x06, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00, 0x00};
        initialized = true;
    }

    const std::vector<uint8_t>& glyph = font_table[static_cast<uint8_t>(c)];
    for (int col = 0; col < 8; ++col) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 8; ++row) {
            if (line & (1 << row)) {
                set_pixel(buf, x + col, y + row);
            }
        }
    }
}

void draw_string(std::vector<uint8_t>& buf, const std::string& str, int x, int y) {
    int current_x = x;
    for (char c : str) {
        draw_char(buf, c, current_x, y);
        current_x += 7; // 文字幅 (8x8 だが文字間隔を空ける)
    }
}

// 雲 + 太陽 + 雨 のお天気アイコンを描画 (24x24サイズ)
void draw_weather_icon(std::vector<uint8_t>& buf, int start_x, int start_y) {
    // 太陽の光線と円
    set_pixel(buf, start_x + 6, start_y + 2);
    set_pixel(buf, start_x + 2, start_y + 6);
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 5; ++c) {
            if ((r-2)*(r-2) + (c-2)*(c-2) <= 5) {
                set_pixel(buf, start_x + 3 + c, start_y + 3 + r);
            }
        }
    }

    // 雲のモコモコした部分 (円を重ね合わせて雲を表現)
    auto draw_circle = [&](int cx, int cy, int radius) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x*x + y*y <= radius*radius) {
                    set_pixel(buf, start_x + cx + x, start_y + cy + y);
                }
            }
        }
    };
    draw_circle(10, 14, 5); // 左側
    draw_circle(16, 11, 7); // 中央上
    draw_circle(22, 13, 6); // 右側
    for (int x = 8; x <= 24; ++x) {
        for (int y = 14; y <= 17; ++y) {
            set_pixel(buf, start_x + x, start_y + y);
        }
    }

    // 雨のしずく (斜めの線)
    for (int i = 0; i < 3; ++i) {
        set_pixel(buf, start_x + 10 + i*5, start_y + 20);
        set_pixel(buf, start_x + 9 + i*5, start_y + 21);
        set_pixel(buf, start_x + 8 + i*5, start_y + 22);
    }
}

// 湿度水滴アイコンを描画 (8x8サイズ)
void draw_humidity_icon(std::vector<uint8_t>& buf, int start_x, int start_y) {
    int widths[] = {1, 3, 5, 7, 7, 5, 3};
    int offsets[] = {3, 2, 1, 0, 0, 1, 2};
    for (int r = 0; r < 7; ++r) {
        for (int c = 0; c < widths[r]; ++c) {
            set_pixel(buf, start_x + offsets[r] + c, start_y + r);
        }
    }
}

int main() {
    std::cout << "[OLED Scenario] Opening I2C device...\n";
    int fd = open(FBB_DEV_PATH_I2C, O_RDWR);
    if (fd < 0) {
        std::cerr << "Failed to open /dev/i2c-0\n";
        return 1;
    }

    // デーモンプロセス (fbb_i2c_oled) が起動してソケットを bind/listen するのを待つ
    std::cout << "[OLED Scenario] Waiting for OLED daemon to start...\n";
    sleep(1); 

    std::cout << "[OLED Scenario] Initializing SSD1306...\n";
    write_cmds(fd, {
        0xAE,       // Display OFF
        0xD5, 0x80, // Set Display Clock Divide Ratio/Oscillator Frequency
        0xA8, 0x3F, // Set Multiplex Ratio (64)
        0xD3, 0x00, // Set Display Offset
        0x40,       // Set Display Start Line (0)
        0x8D, 0x14, // Set Charge Pump (Enable)
        0x20, 0x00, // Set Memory Addressing Mode (Horizontal Addressing Mode)
        0xA1,       // Set Segment Re-map (COL127 to SEG0)
        0xC8,       // Set COM Output Scan Direction (COM63 to COM0)
        0xDA, 0x12, // Set COM Pins Hardware Configuration
        0x81, 0xCF, // Set Contrast Control (0xCF)
        0xD9, 0xF1, // Set Pre-charge Period
        0xDB, 0x40, // Set VCOMH Deselect Level
        0xA4,       // Entire Display ON (Resume to GDDRAM content)
        0xA6,       // Set Normal Display
        0xAF        // Display ON
    });

    std::cout << "[OLED Scenario] Display initialized successfully.\n";

    // アニメーションパターン定義
    std::vector<std::vector<uint8_t>> patterns;

    // パターン1: 市模樣 (Checkerboard)
    // @intent:rationale 縦8x横8ドットの正方形で構成された綺麗な市松模様（チェッカーボード）を表現するため、ページ（縦8ドット）単位で 0xFF と 0x00 を交互に割り当てます。
    std::vector<uint8_t> p1(1024, 0);
    for (size_t page = 0; page < 8; ++page) {
        for (size_t col = 0; col < 128; ++col) {
            p1[page * 128 + col] = ((page % 2) == (col / 8 % 2)) ? 0xFF : 0x00;
        }
    }
    patterns.push_back(p1);

    // パターン2: 横縞 (Horizontal Stripes)
    std::vector<uint8_t> p2(1024, 0);
    for (size_t page = 0; page < 8; ++page) {
        for (size_t col = 0; col < 128; ++col) {
            p2[page * 128 + col] = 0xC3;
        }
    }
    patterns.push_back(p2);

    // パターン3: 縦縞 (Vertical Stripes)
    std::vector<uint8_t> p3(1024, 0);
    for (size_t page = 0; page < 8; ++page) {
        for (size_t col = 0; col < 128; ++col) {
            p3[page * 128 + col] = ((col % 8) < 4) ? 0xFF : 0x00;
        }
    }
    patterns.push_back(p3);

    // パターン4: 外枠と対角クロス (Border & Cross)
    std::vector<uint8_t> p4(1024, 0);
    for (size_t page = 0; page < 8; ++page) {
        for (size_t col = 0; col < 128; ++col) {
            size_t addr = page * 128 + col;
            uint8_t val = 0x00;
            if (page == 0) val |= 0x01;
            if (page == 7) val |= 0x80;
            if (col == 0 || col == 127) val |= 0xFF;
            size_t row_base = page * 8;
            for (size_t r = 0; r < 8; ++r) {
                size_t current_row = row_base + r;
                if (col == current_row * 2 || col == (63 - current_row) * 2) {
                    val |= (1 << r);
                }
            }
            p4[addr] = val;
        }
    }
    patterns.push_back(p4);

    // パターン5: お天気ダッシュボード (Weather Dashboard)
    // @intent:rationale 128x64の解像度枠内に文字やアイコンが重なることなく完璧に収まり、読みやすくなるよう描画座標を調整しました。
    std::vector<uint8_t> p5(1024, 0);
    // 文字列の描画
    draw_string(p5, "Temp.", 46, 4);
    
    draw_string(p5, "inside", 46, 20);
    draw_string(p5, "24*C", 98, 20);
    
    draw_string(p5, "outside", 46, 34);
    draw_string(p5, "29*C", 98, 34);
    
    draw_string(p5, "Humidity", 36, 48);
    draw_humidity_icon(p5, 96, 48);
    draw_string(p5, "64%", 105, 48);

    // お天気アイコン
    draw_weather_icon(p5, 12, 16);
    
    patterns.push_back(p5);

    std::cout << "[OLED Scenario] Starting animation loop. Press Ctrl+C to stop.\n";
    int pattern_idx = 0;
    while (true) {
        std::cout << "[OLED Scenario] Drawing Pattern " << (pattern_idx + 1) << "...\n";
        
        // アドレスを先頭にリセットし、かつ Addressing Mode を毎回設定して堅牢性を確保する
        write_cmds(fd, {
            0x20, 0x00,   // Set Memory Addressing Mode (Horizontal Addressing Mode)
            0x21, 0, 127, // Set Column Address Range
            0x22, 0, 7    // Set Page Address Range
        });

        // パターンの送信
        write_data(fd, patterns[pattern_idx]);

        pattern_idx = (pattern_idx + 1) % patterns.size();
        sleep(2); // 2秒おきに切り替え
    }

    close(fd);
    return 0;
}
