"""
@file scripts/vfpga/generator_rtl.py
@intent:responsibility
    DTS モデル（BoardModel）を受け取り、RTL スケルトン（Verilog）、Verilator シミュレータラッパー（C++）、
    Web ダッシュボード用マニフェスト（board_manifest.json）、および Rust PAC（Peripheral Access Crate）を生成する。
@intent:rationale
    Single Source of Truth 原則に基づき、Verilog・C++ シミュレータ・Rust PAC・Web UI メタデータの全階層を
    DTS から 100% 自動同期させることで、異種言語・異種ドメイン間でのアドレス・ビット幅の食い違いを完全に防止する。
"""

import os
import sys
import json
from vfpga.models import BoardModel
from vfpga.generator_base import BaseGenerator, SystemConfigGenerator


class RTLGenerator(BaseGenerator):
    """
    @class RTLGenerator
    @intent:responsibility
        DTS のレジスタ定義から Verilog RTL トップモジュール（vfpga_top.v）のスケルトンを生成。
    @intent:rationale
        クロック同期、リセット解除、アドレスデコード、w_en/w_data 書き込み、r_data 読み出しロジックを
        DTS から自動生成し、RTL 設計者がカスタムロジックに集中できるようにする。
    """

    def generate(self, model: BoardModel):
        devs = model.get_uio_devices()
        if not devs:
            return """module vfpga_top (
    /* verilator lint_off UNUSED */
    input wire clk, input wire rst_n, input wire [31:0] addr, 
    input wire [31:0] w_data, input wire w_en, output reg [31:0] r_data
    /* verilator lint_on UNUSED */
);
    always @(*) r_data = 32'hdeadbeef;
endmodule"""
        # 全UIO/GPIOデバイスのレジスタを物理アドレス付きで集約
        all_regs = []
        for dev in devs:
            for r in dev.registers:
                phys_addr = dev.base_addr + int(r.offset, 0)
                all_regs.append((r.name, phys_addr))

        reg_ports = ",\n".join(["    output reg [31:0] %s" % name for name, _ in all_regs])
        reset_logic = "\n".join(["            %s <= 32'h0;" % name for name, _ in all_regs])
        write_cases = "\n".join(["                32'h%08x: %s <= w_data;" % (addr, name) for name, addr in all_regs])
        read_cases = "\n".join(["            32'h%08x: r_data = %s;" % (addr, name) for name, addr in all_regs])
        return """/* Auto-generated RTL Skeleton */
/* verilator lint_off UNUSED */
module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output reg [31:0] r_data,
    input wire [117:0] l_pins_i,
    /* verilator lint_on UNUSED */
    output wire [117:0] l_pins_o,
    output wire [117:0] l_pins_t%s
);
    // GPIO Pin Mapping Logic (Generic Placeholder)
    assign l_pins_o = 118'h0;
    assign l_pins_t = 118'hFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF; // All inputs by default

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            %s
        end else if (w_en) begin
            case (addr)
%s
                default: ;
            endcase
        end
    end
    always @(*) begin
        case (addr)
%s
            default: r_data = 32'h0;
        endcase
    end
endmodule
""" % (
            ("," + reg_ports) if reg_ports else "",
            reset_logic,
            write_cases,
            read_cases,
        )


class SimulatorGenerator(BaseGenerator):
    """
    @class SimulatorGenerator
    @intent:responsibility
        Verilator C++ シミュレータの駆動メインループ（main.cpp）を生成する。
    @intent:rationale
        共有メモリ（SHM）と Verilated RTL トップモジュール間の高速ポーリング同期、
        PL SPI ブリッジ通信、UIO 割り込みソケット通知、および VCD 波形ダンプを一括管理する。
    """

    def generate(self, model: BoardModel):
        devs = model.get_uio_devices()
        # 全UIO/GPIOデバイスのレジスタを物理アドレスで集約
        reg_defs = []
        for dev in devs:
            for r in dev.registers:
                phys_addr = dev.base_addr + int(r.offset, 0)
                reg_defs.append('    { .name="%s", .addr=0x%08x }' % (r.name, phys_addr))

        # SHMベースアドレス (最小のデバイスベースアドレス)
        min_base = min(d.base_addr for d in devs) if devs else 0

        return """
#include <stdio.h>
#include <iostream>
#include <verilated.h>
#include "Vvfpga_top.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <verilated_vcd_c.h>
#include "vfpga_system_config.h"
#include "sim_traits.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

double sc_time_stamp() { return 0; }

class PlSpiBridge {
private:
    int sock_fd = -1;
    std::string socket_path;
    bool connected = false;

    uint8_t last_sclk = 0;
    uint8_t last_cs = 1;
    uint8_t bit_count = 0;
    uint8_t tx_shift_reg = 0;
    uint8_t rx_shift_reg = 0;
    std::vector<uint8_t> accumulated_tx;
    uint8_t last_rx_buf[3] = {0};

public:
    PlSpiBridge(const std::string& path) : socket_path(path) {}
    ~PlSpiBridge() { close_socket(); }

    void connect_socket() {
        if (connected) return;
        if (socket_path.empty()) return;
        
        sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd < 0) return;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path)-1);

        int ret = -1;
        for (int i = 0; i < 20; i++) { // 最大2秒間リトライ
            ret = connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr));
            if (ret == 0) break;
            usleep(100000); // 100ms 待機
        }

        if (ret == 0) {
            connected = true;
        } else {
            close(sock_fd);
            sock_fd = -1;
        }
    }

    void close_socket() {
        if (sock_fd >= 0) {
            close(sock_fd);
            sock_fd = -1;
        }
        connected = false;
    }

    template <typename T>
    void tick(T* top) {
        if constexpr (has_pl_spi_sclk<T>::value && has_pl_spi_mosi<T>::value && 
                      has_pl_spi_cs_n<T>::value && has_pl_spi_miso<T>::value) {
            
            uint8_t cs = top->pl_spi_cs_n;
            uint8_t sclk = top->pl_spi_sclk;
            uint8_t mosi = top->pl_spi_mosi;

             if (cs && !last_cs) {
                bit_count = 0;
                tx_shift_reg = 0;
            }

            if (!cs) {
                if (last_cs) {
                    // CSのアサート（立ち下がり）時に、前回のトランザクション(4バイト以上)が残っていればクリア
                    if (accumulated_tx.size() >= 4) {
                        accumulated_tx.clear();
                    }
                    // 最初のビット(ビット7)をアサート直後にピンに出力しておく
                    top->pl_spi_miso = (rx_shift_reg >> 7) & 0x1;
                    rx_shift_reg <<= 1;
                }
                if (!connected) connect_socket();
                
                // 立ち上がりで MOSI サンプリング
                if (sclk && !last_sclk) {
                    tx_shift_reg = (tx_shift_reg << 1) | (mosi & 0x1);
                    bit_count++;

                    if (bit_count == 8) {
                        accumulated_tx.push_back(tx_shift_reg);
                        uint8_t resp = 0xFF;
                        size_t total = accumulated_tx.size();
                        
                        if (total <= 3) {
                            if (connected) {
                                uint16_t len = 3;
                                uint8_t tx_buf[3] = {0};
                                for (size_t i = 0; i < 3; ++i) {
                                    if (i < total) {
                                        tx_buf[i] = accumulated_tx[i];
                                    }
                                }
                                
                                send(sock_fd, &len, sizeof(len), 0);
                                send(sock_fd, tx_buf, 3, 0);
                                
                                int n = recv(sock_fd, last_rx_buf, 3, MSG_WAITALL);
                                if (n > 0) {
                                    resp = last_rx_buf[total - 1];
                                } else {
                                    close_socket();
                                }
                            }
                        } else {
                            // 4バイト目以降はソケット通信せず、前回の応答バッファから現在完了したバイトインデックスに相当する応答（通常は下位8ビット = last_rx_buf[2]）を再利用
                            size_t target_idx = (total - 1);
                            if (target_idx < 3) {
                                resp = last_rx_buf[target_idx];
                            } else {
                                resp = last_rx_buf[2]; // 3バイト目の応答でフォールバック
                            }
                        }
                        
                        rx_shift_reg = resp;
                        bit_count = 0;
                        tx_shift_reg = 0;
                    }
                }
                
                // 立ち下がりで MISO 更新
                if (!sclk && last_sclk) {
                    top->pl_spi_miso = (rx_shift_reg >> 7) & 0x1;
                    rx_shift_reg <<= 1;
                }
            }

            last_sclk = sclk;
            last_cs = cs;
        }
    }
};

VerilatedVcdC* volatile_trace = nullptr;

void handle_sigterm(int sig) {
    if (volatile_trace) {
        volatile_trace->close();
    }
    _exit(sig);
}

#define SHM_BASE_ADDR 0x%08xU

struct RegMeta { const char* name; uint32_t addr; };
static RegMeta registers[] = { %s };

template <typename T>
void run_sim_loop(T* top, volatile uint32_t* shm, uint32_t* old_shm, VerilatedVcdC* m_trace, uint64_t& vtime) {
    PlSpiBridge bridge(PL_SPI_SOCKET);
    // Initial Reset Sequence
    if constexpr (has_rst_n<T>::value) top->rst_n = 1;
    if constexpr (has_clk<T>::value) top->clk = 0;
    top->eval(); m_trace->dump(vtime++);
    
    if constexpr (has_rst_n<T>::value) top->rst_n = 0;
    top->eval(); 
    if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
    m_trace->dump(vtime++);
    
    if constexpr (has_rst_n<T>::value) top->rst_n = 1;
    top->eval(); m_trace->dump(vtime++);

    printf("[Sim] Simulator Started (SHM: %%s)\\n", SHM_FILE); fflush(stdout);
        int loop_count = 0;
        while (!Verilated::gotFinish()) {
            if (loop_count++ %% 10000 == 0) {
                printf("[Sim Debug] ");
                for (int j = 0; j < 16; j++) {
                    printf("shm[%%d]=0x%%x ", j, shm[j]);
                }
                printf("\\n"); fflush(stdout);
            }
            // Synchronize Write from SHM to RTL
            for (int i = 0; i < %d; i++) {
                uint32_t off = (registers[i].addr - SHM_BASE_ADDR) / 4;
                if (shm[off] != old_shm[off]) {
                    if constexpr (has_addr<T>::value) top->addr = registers[i].addr;
                    if constexpr (has_w_data<T>::value) top->w_data = shm[off];
                    if constexpr (has_w_en<T>::value) top->w_en = 1;
                    
                    top->eval(); 
                    if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
                    
                    if constexpr (has_w_en<T>::value) top->w_en = 0;
                    old_shm[off] = shm[off];

                    // Virtual IPI Register (TRIG) detection, signal relay and auto-clear
                    if (strcmp(registers[i].name, "TRIG") == 0 && shm[off] == 0) {
                        FILE* pf = fopen("/tmp/fbb/sys/class/remoteproc/remoteproc0/proxy_pid", "r");
                        if (pf) {
                            int pid = 0;
                            if (fscanf(pf, "%%d", &pid) == 1 && pid > 0) {
                                kill(pid, SIGUSR2);
                            }
                            fclose(pf);
                        }
                        shm[off] = 0xFFFFFFFF;
                        old_shm[off] = 0xFFFFFFFF;
                        if constexpr (has_addr<T>::value) top->addr = registers[i].addr;
                        if constexpr (has_w_data<T>::value) top->w_data = 0xFFFFFFFF;
                        if constexpr (has_w_en<T>::value) top->w_en = 1;
                        top->eval();
                        if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
                        if constexpr (has_w_en<T>::value) top->w_en = 0;
                    }
                }
            }

            // UI Injection Handling for Input Pins
            // If TRI bit is 1, apply SHM value to l_pins_i
            // (Assuming last 4 words of SHM are injection area)
            for (int b = 0; b < 118; b++) {
                int word = b / 32;
                int bit = b %% 32;
                // Simplified: Mapping logic should match Dashboard's gpio-inject
                // top->l_pins_i[b] = (shm[SHM_SIZE/4 - 4 + word] >> bit) & 0x1;
            }

            // Synchronize Read from RTL to SHM
            for (int i = 0; i < %d; i++) {
                if constexpr (has_addr<T>::value) top->addr = registers[i].addr;
                top->eval();
                uint32_t off = (registers[i].addr - SHM_BASE_ADDR) / 4;
                
                if constexpr (has_r_data<T>::value) {
                    if (top->r_data != 0xdeadbeef && top->r_data != old_shm[off]) {
                        shm[off] = top->r_data; old_shm[off] = top->r_data;
                    }
                }
            }
            top->eval(); 
            if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
            if constexpr (has_irq_out<T>::value) {
                static int last_irq = 0;
                if (top->irq_out && !last_irq) {
                    int irq_sock = socket(AF_UNIX, SOCK_STREAM, 0);
                    if (irq_sock >= 0) {
                        struct sockaddr_un irq_addr;
                        memset(&irq_addr, 0, sizeof(irq_addr));
                        irq_addr.sun_family = AF_UNIX;
                        strncpy(irq_addr.sun_path, "/tmp/fbb_uio_irq_0", sizeof(irq_addr.sun_path) - 1);
                        if (connect(irq_sock, (struct sockaddr*)&irq_addr, sizeof(irq_addr)) == 0) {
                            uint64_t val = 1;
                            send(irq_sock, &val, sizeof(val), 0);
                        }
                        close(irq_sock);
                    }
                }
                last_irq = top->irq_out;
            }
            m_trace->dump(vtime++);
            bridge.tick(top);
            usleep(100);
        }
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vvfpga_top* top = new Vvfpga_top;
    int fd = open(SHM_FILE, O_CREAT | O_RDWR, 0666);
    if (ftruncate(fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        return 1;
    }
    volatile uint32_t* shm = (volatile uint32_t*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    uint32_t* old_shm = new uint32_t[SHM_SIZE/4]; memset(old_shm, 0xFF, SHM_SIZE);

    Verilated::traceEverOn(true);
    VerilatedVcdC* m_trace = new VerilatedVcdC;
    volatile_trace = m_trace;
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);
    top->trace(m_trace, 99);
    m_trace->open("vfpga.vcd");
    uint64_t vtime = 0;
    
    run_sim_loop(top, shm, old_shm, m_trace, vtime);

    m_trace->close();
    delete[] old_shm;
    return 0;
}
""" % (
            min_base,
            ", ".join(reg_defs),
            len(reg_defs),
            len(reg_defs),
        )


class ManifestGenerator(BaseGenerator):
    """
    @class ManifestGenerator
    @intent:responsibility
        Web ダッシュボード（React/Express）が使用する board_manifest.json を出力する。
    @intent:rationale
        PPA プラグイン（OLED, 7seg, ADC, Flash, HUB75 等）の UI ウィジェット設定、
        共有メモリパス、UART ポート番号、レジスタリストを単一 JSON に集約し、
        ダッシュボードが外部設定なしで自動的にリアルタイム可視化 UI を構成できるようにする。
    """

    # @intent:rationale Webダッシュボードのバックエンドが、現在ロードされているシナリオのディレクトリ（config.dtsの配置場所）を特定し、そこにレイアウトファイルを保存・復元できるようにするため、メタデータに scenario_dir を追加します。
    def generate(self, model: BoardModel):
        shm_name = model.name
        shm_size = SystemConfigGenerator.compute_shm_size(model)
        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
        manifest = {
            "board": shm_name,
            "model": getattr(model, "model_name", "generic-vfpga"),
            "shm_path": f"/tmp/{shm_name}",
            "shm_size": shm_size,
            "project_root": project_root,
            "scenario_dir": getattr(model, "scenario_dir", ""),
            "hdmi_output_path": "/tmp/hdmi_output.bmp",
            "devices": [],
            "uarts": [
                {"name": d.name, "port": int(d.extra_props.get("port", 2000 + idx))}
                for idx, d in enumerate(model.get_uart_devices())
            ],
        }
        # Discover PPA plugin UI widgets (ADR #005)
        sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "../../src/controller")))
        try:
            from vlogic_controller import discover_plugins

            discovered_plugins = discover_plugins(getattr(model, "scenario_dir", ""))
        except Exception:
            discovered_plugins = {}

        def find_plugin_for_compat(compat_str):
            import re

            if not compat_str or not discovered_plugins:
                return None
            if compat_str in discovered_plugins:
                return discovered_plugins[compat_str]
            for k, v in discovered_plugins.items():
                if compat_str in k or k in compat_str:
                    return v
            items = re.findall(r"[a-zA-Z0-9_-]+,[a-zA-Z0-9_-]+", compat_str)
            for item in items:
                if item in discovered_plugins:
                    return discovered_plugins[item]
                for k, v in discovered_plugins.items():
                    if item in k or k in item:
                        return v
            for item in re.findall(r"[a-zA-Z0-9_-]+", compat_str):
                if item in discovered_plugins:
                    return discovered_plugins[item]
            return None

        for dev in model.devices:
            dev_info = {
                "name": dev.name,
                "type": dev.type,
                "path": dev.path,
                "base_addr": dev.base_addr,
                "base_reg": dev.base_reg,
                "registers": [
                    {
                        "name": r.name,
                        "logical_name": r.logical_name or r.name,
                        "offset": r.offset,
                        "direction_mode": getattr(r, "direction_mode", None),
                    }
                    for r in dev.registers
                ],
                "extra": dev.extra_props,
            }
            compat_str = (
                (
                    dev.extra_props.get("compatible")
                    if hasattr(dev, "extra_props") and isinstance(dev.extra_props, dict)
                    else getattr(dev, "compatible", None)
                )
                or "generic,hub75-matrix"
                if "matrix" in dev.name
                else None
            )
            if compat_str:
                plugin_info = find_plugin_for_compat(compat_str)
                if plugin_info:
                    dev_info["compatible"] = compat_str
                    if plugin_info.get("ui_widget"):
                        ui_w = dict(plugin_info.get("ui_widget"))
                        ui_w["title"] = ui_w.get("title", dev.name)
                        # Override ui_widget properties from DTS if specified
                        if isinstance(dev.extra_props, dict):
                            if "grid_size" in dev.extra_props:
                                gs = dev.extra_props["grid_size"]
                                if isinstance(gs, str):
                                    try:
                                        gs = [int(x) for x in gs.split()]
                                    except Exception:
                                        pass
                                dev_info["grid_size"] = gs
                            if "shm_name" in dev.extra_props:
                                dev_info["shm_name"] = dev.extra_props["shm_name"]
                                dev_info["shm_path"] = f"/dev/shm/{dev.extra_props['shm_name']}"
                            if "panel_count" in dev.extra_props:
                                dev_info["panel_count"] = dev.extra_props["panel_count"]
                            if "chain_layout" in dev.extra_props:
                                dev_info["chain_layout"] = dev.extra_props["chain_layout"]

                            if "controls" in ui_w and isinstance(ui_w["controls"], list):
                                new_controls = []
                                for ctrl in ui_w["controls"]:
                                    ctrl_copy = dict(ctrl)
                                    if "grid_size" in dev.extra_props:
                                        gs = dev.extra_props["grid_size"]
                                        if isinstance(gs, str):
                                            try:
                                                gs = [int(x) for x in gs.split()]
                                            except Exception:
                                                pass
                                        ctrl_copy["grid_size"] = gs
                                    if "shm_name" in dev.extra_props:
                                        ctrl_copy["shm_file"] = dev.extra_props["shm_name"]
                                    new_controls.append(ctrl_copy)
                                ui_w["controls"] = new_controls
                        dev_info["ui_widget"] = ui_w
                    if plugin_info.get("shm_path") and "shm_path" not in dev_info:
                        dev_info["shm_path"] = plugin_info.get("shm_path")

            if dev.type == "i2c" and hasattr(dev, "i2c_slaves"):
                slaves = []
                for s in dev.i2c_slaves:
                    s_info = {"name": s.name, "addr": s.addr, "compatible": s.compatible}
                    plugin_info = find_plugin_for_compat(s.compatible)
                    if plugin_info and plugin_info.get("ui_widget"):
                        ui_w = dict(plugin_info.get("ui_widget"))
                        base_title = ui_w.get("title", s.name)
                        ui_w["title"] = f"{base_title} (0x{s.addr:02X})"
                        s_info["ui_widget"] = ui_w
                    slaves.append(s_info)
                dev_info["i2c_slaves"] = slaves
            if dev.type == "spi" and hasattr(dev, "spi_slaves"):
                slaves = []
                for s in dev.spi_slaves:
                    s_info = {"name": s.name, "cs": s.cs, "compatible": s.compatible}
                    plugin_info = find_plugin_for_compat(s.compatible)
                    if plugin_info and plugin_info.get("ui_widget"):
                        ui_w = dict(plugin_info.get("ui_widget"))
                        base_title = ui_w.get("title", s.name)
                        ui_w["title"] = f"{base_title} (CS{s.cs})"
                        s_info["ui_widget"] = ui_w
                    slaves.append(s_info)
                dev_info["spi_slaves"] = slaves
            manifest["devices"].append(dev_info)
        return json.dumps(manifest, indent=4)


class RustPACGenerator(BaseGenerator):
    """
    @class RustPACGenerator
    @intent:responsibility
        Rust ファームウェア用の PAC (Peripheral Access Crate) ソースコードを生成する。
    @intent:rationale
        Rust の所有権モデルおよび volatile アクセス API に適合したレジスタ抽象構造体を提供し、
        Peripherals::take() シングルトンパターンによる安全なハードウェアアクセスを実現する。
    """

    def generate(self, model: BoardModel) -> str:
        devs = model.get_uio_devices()
        if not devs:
            return "// No UIO/GPIO devices defined in DTS for PAC generation.\n"

        struct_fields = []
        reg_instantiations = []

        for dev in devs:
            for r in dev.registers:
                phys_addr = dev.base_addr + int(r.offset, 0)
                field_name = r.name.lower()
                struct_fields.append(f"    pub {field_name}: Register<u32>,")
                reg_instantiations.append(f"            {field_name}: Register::new(0x{phys_addr:08x}),")

        struct_fields_str = "\n".join(struct_fields)
        reg_instantiations_str = "\n".join(reg_instantiations)

        return f"""// Auto-generated PAC (Peripheral Access Crate)
#![allow(unused)]

pub struct Register<T> {{
    ptr: *mut T,
}}

impl<T> Register<T> {{
    pub const fn new(address: usize) -> Self {{
        Self {{ ptr: address as *mut T }}
    }}
    
    pub fn read(&self) -> T where T: Copy {{
        unsafe {{ core::ptr::read_volatile(self.ptr) }}
    }}
    
    pub fn write(&self, value: T) where T: Copy {{
        unsafe {{ core::ptr::write_volatile(self.ptr, value) }}
    }}
}}

pub struct Vfpga {{
{struct_fields_str}
}}

impl Vfpga {{
    pub const fn new() -> Self {{
        Self {{
{reg_instantiations_str}
        }}
    }}
}}

pub struct Peripherals {{
    pub vfpga: Vfpga,
}}

static mut TAKEN: bool = false;

impl Peripherals {{
    pub fn take() -> Option<Self> {{
        unsafe {{
            if TAKEN {{
                None
            }} else {{
                TAKEN = true;
                Some(Self {{
                    vfpga: Vfpga::new(),
                }})
            }}
        }}
    }}
}}
"""
