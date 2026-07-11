import os
import json
from vfpga.models import BoardModel
from vfpga.generator_base import BaseGenerator, ConfigGenerator

class RTLGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        devs = model.get_uio_devices()
        if not devs: return """/* verilator lint_off UNUSED */
module vfpga_top (
    input wire clk, input wire rst_n, input wire [31:0] addr, 
    input wire [31:0] w_data, input wire w_en, output reg [31:0] r_data
);
    always @(*) r_data = 32'hdeadbeef;
endmodule"""
        # 全UIO/GPIOデバイスのレジスタを物理アドレス付きで集約
        all_regs = []
        for dev in devs:
            for r in dev.registers:
                phys_addr = dev.base_addr + int(r.offset, 0)
                all_regs.append((r.name, phys_addr))

        reg_ports = ",\n".join(['    output reg [31:0] %s' % name for name, _ in all_regs])
        reset_logic = "\n".join(['            %s <= 32\'h0;' % name for name, _ in all_regs])
        write_cases = "\n".join(["                32'h%08x: %s <= w_data;" % (addr, name) for name, addr in all_regs])
        read_cases = "\n".join(["            32'h%08x: r_data = %s;" % (addr, name) for name, addr in all_regs])
        return """/* Auto-generated RTL Skeleton */
module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output reg [31:0] r_data,
    /* verilator lint_off UNUSED */
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
            default: r_data = 32'hdeadbeef;
        endcase
    end
endmodule
""" % (("," + reg_ports) if reg_ports else "", reset_logic, write_cases, read_cases)

class SimulatorGenerator(BaseGenerator):
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
#include "vfpga_config.h"
#include "sim_traits.h"

double sc_time_stamp() { return 0; }

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
                    if (top->r_data != old_shm[off]) {
                        shm[off] = top->r_data; old_shm[off] = top->r_data;
                    }
                }
            }
            top->eval(); 
            if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
            m_trace->dump(vtime++);
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
    uint32_t* old_shm = new uint32_t[SHM_SIZE/4]; memset(old_shm, 0, SHM_SIZE);

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
""" % (min_base, ", ".join(reg_defs), len(reg_defs), len(reg_defs))

class ManifestGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        shm_name = model.name
        shm_size = ConfigGenerator.compute_shm_size(model)
        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
        manifest = {
            "board": shm_name,
            "model": getattr(model, "model_name", "generic-vfpga"),
            "shm_path": f"/tmp/{shm_name}",
            "shm_size": shm_size,
            "project_root": project_root,
            "hdmi_output_path": "/tmp/hdmi_output.bmp",
            "devices": [],
            "uarts": [{"name": d.name, "port": int(d.extra_props.get("port", 2000))} for d in model.get_uart_devices()]
        }
        for dev in model.devices:
            dev_info = {
                "name": dev.name,
                "type": dev.type,
                "path": dev.path,
                "base_addr": dev.base_addr,
                "base_reg": dev.base_reg,
                "registers": [{"name": r.name, "logical_name": r.logical_name or r.name, "offset": r.offset} for r in dev.registers],
                "extra": dev.extra_props
            }
            if dev.type == 'i2c' and hasattr(dev, 'i2c_slaves'):
                dev_info["i2c_slaves"] = [{"name": s.name, "addr": s.addr, "compatible": s.compatible} for s in dev.i2c_slaves]
            if dev.type == 'spi' and hasattr(dev, 'spi_slaves'):
                dev_info["spi_slaves"] = [{"name": s.name, "cs": s.cs, "compatible": s.compatible} for s in dev.spi_slaves]
            manifest["devices"].append(dev_info)
        return json.dumps(manifest, indent=4)

class RustPACGenerator(BaseGenerator):
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
