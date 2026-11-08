#!/usr/bin/env python3
"""
@file scripts/tests/test_v2gtr.py
@intent:responsibility
    v2gtr (Verilog-to-GTR 逆生成機能) の単体テストおよび F-BB DTSParser との結合テストを行う。
@intent:rationale
    既存シナリオの完成版 config.gtr を Golden Reference（正解データ）として非破壊照合し、
    また新規生成された DTS/GTR が F-BB のパーサーを例外なく通過することを検証する。
@intent:pre-condition
    Python 3.8 以上であり、scripts/ 配下のモジュールがインポート可能であること。
"""

import os
import sys
import unittest
import tempfile

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from vfpga.importer_verilog import VerilogImporter
from vfpga.emitter_gtr import GtrEmitter
from vfpga.parser import DTSParser


class TestV2Gtr(unittest.TestCase):

    def setUp(self):
        self.proj_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))

    def test_golden_01_standard_uio(self):
        """
        @intent:responsibility
            01_standard_uio の vfpga_top.v から RST, EN, CNT レジスタが
            正確なオフセットとアクセス権限で抽出されるかを非破壊で検証する。
        """
        v_path = os.path.join(self.proj_root, "tests/scenarios/01_standard_uio/vfpga_top.v")
        self.assertTrue(os.path.exists(v_path), f"File not found: {v_path}")

        importer = VerilogImporter.from_file(v_path)
        devices = importer.extract_devices()

        self.assertEqual(len(devices), 1)
        dev = devices[0]
        self.assertEqual(dev.base_addr, 0x40000000)

        reg_map = {r.name: (r.offset, r.direction) for r in dev.registers}
        self.assertIn("RST", reg_map)
        self.assertIn("EN", reg_map)
        self.assertIn("CNT", reg_map)

        self.assertEqual(reg_map["RST"], ("0x10", "RW"))
        self.assertEqual(reg_map["EN"], ("0x14", "RW"))
        self.assertEqual(reg_map["CNT"], ("0x18", "RO"))

    def test_golden_02c_pl_spi(self):
        """
        @intent:responsibility
            02c_pl_spi の vfpga_top.v から、SPI と GPIO の 2 つのデバイス領域が分離され、
            各レジスタ（TX, RX, CTRL, STATUS, DATA, TRI）が正確に抽出されるかを検証する。
        """
        v_path = os.path.join(self.proj_root, "tests/scenarios/02c_pl_spi/vfpga_top.v")
        self.assertTrue(os.path.exists(v_path), f"File not found: {v_path}")

        importer = VerilogImporter.from_file(v_path)
        devices = importer.extract_devices()

        self.assertEqual(len(devices), 2)

        # Device 1: pl_spi_ctrl @ 0x40000000
        spi_dev = next((d for d in devices if d.base_addr == 0x40000000), None)
        self.assertIsNotNone(spi_dev)
        spi_regs = {r.name: (r.offset, r.direction) for r in spi_dev.registers}

        self.assertEqual(spi_regs.get("REG_SPI_TX"), ("0x00", "RW"))
        self.assertEqual(spi_regs.get("REG_SPI_RX"), ("0x04", "RO"))
        self.assertEqual(spi_regs.get("REG_SPI_CTRL"), ("0x08", "RW"))
        self.assertEqual(spi_regs.get("REG_SPI_STATUS"), ("0x0c", "RO"))

        # Device 2: vfpga_gpio @ 0x40002000
        gpio_dev = next((d for d in devices if d.base_addr == 0x40002000), None)
        self.assertIsNotNone(gpio_dev)
        gpio_regs = {r.name: (r.offset, r.direction) for r in gpio_dev.registers}

        self.assertEqual(gpio_regs.get("DATA"), ("0x00", "RO"))
        self.assertEqual(gpio_regs.get("TRI"), ("0x04", "RO"))

    def test_synthetic_vivado_axi_lite(self):
        """
        @intent:responsibility
            Vivado 標準の AXI-Lite スレーブテンプレート（ADDR_LSB=2 スライスアドレスデコード）
            からバイトオフセット（0x00, 0x04, 0x08, 0x0c）が正確に復元されるかを検証する。
        """
        verilog_code = """
        module my_axi_ip #(
            parameter integer C_S_AXI_DATA_WIDTH = 32,
            parameter integer C_S_AXI_ADDR_WIDTH = 4
        ) (
            input wire S_AXI_ACLK,
            input wire S_AXI_ARESETN,
            input wire [C_S_AXI_ADDR_WIDTH-1 : 0] S_AXI_AWADDR,
            input wire S_AXI_WVALID,
            input wire [C_S_AXI_DATA_WIDTH-1 : 0] S_AXI_WDATA,
            input wire [C_S_AXI_ADDR_WIDTH-1 : 0] S_AXI_ARADDR,
            output reg [C_S_AXI_DATA_WIDTH-1 : 0] S_AXI_RDATA
        );
            localparam integer ADDR_LSB = 2;
            localparam integer OPT_MEM_ADDR_BITS = 1;

            reg [31:0] slv_reg0;
            reg [31:0] slv_reg1;
            reg [31:0] slv_reg2;
            reg [31:0] slv_reg3;

            always @(posedge S_AXI_ACLK) begin
                if (S_AXI_WVALID) begin
                    case (S_AXI_AWADDR[ADDR_LSB+OPT_MEM_ADDR_BITS:ADDR_LSB])
                        2'h0: slv_reg0 <= S_AXI_WDATA;
                        2'h1: slv_reg1 <= S_AXI_WDATA;
                        2'h2: slv_reg2 <= S_AXI_WDATA;
                        2'h3: slv_reg3 <= S_AXI_WDATA;
                        default: ;
                    endcase
                end
            end

            always @(*) begin
                case (S_AXI_ARADDR[ADDR_LSB+OPT_MEM_ADDR_BITS:ADDR_LSB])
                    2'h0: S_AXI_RDATA = slv_reg0;
                    2'h1: S_AXI_RDATA = slv_reg1;
                    2'h2: S_AXI_RDATA = slv_reg2;
                    2'h3: S_AXI_RDATA = slv_reg3;
                    default: S_AXI_RDATA = 32'd0;
                endcase
            end
        endmodule
        """
        importer = VerilogImporter(verilog_code, default_base_addr=0x43C00000, device_name_override="my_axi_ip")
        devices = importer.extract_devices()

        self.assertEqual(len(devices), 1)
        dev = devices[0]
        self.assertEqual(dev.base_addr, 0x43C00000)
        self.assertEqual(len(dev.registers), 4)

        offsets = [r.offset for r in dev.registers]
        self.assertEqual(offsets, ["0x00", "0x04", "0x08", "0x0c"])
        for r in dev.registers:
            self.assertEqual(r.direction, "RW")

    def test_e2e_fbb_parser_integration(self):
        """
        @intent:responsibility
            v2gtr で一時ディレクトリに生成された config.dts および config.gtr を、
            F-BB 本体の DTSParser が構文エラーなく読み込み、BoardModel を構築できるかを検証する。
        """
        v_path = os.path.join(self.proj_root, "tests/scenarios/01_standard_uio/vfpga_top.v")
        importer = VerilogImporter.from_file(v_path)
        devices = importer.extract_devices()

        with tempfile.TemporaryDirectory() as tmpdir:
            dts_p, gtr_p = GtrEmitter.write_scenario_pair(devices, tmpdir)

            self.assertTrue(os.path.exists(dts_p))
            self.assertTrue(os.path.exists(gtr_p))

            # F-BB 本体の DTSParser でパース
            model = DTSParser.parse(gtr_p)
            self.assertIsNotNone(model)

            uio_dev = model.get_uio_device()
            self.assertIsNotNone(uio_dev)
            self.assertEqual(uio_dev.base_addr, 0x40000000)

            parsed_reg_names = [r.name for r in uio_dev.registers]
            self.assertEqual(parsed_reg_names, ["RST", "EN", "CNT"])


if __name__ == "__main__":
    unittest.main()

