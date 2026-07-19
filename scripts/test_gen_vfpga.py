import unittest
import os
import sys
from gen_vfpga import DTSParser, BoardModel, SystemConfigGenerator, DeviceConfigGenerator, ShimGenerator, RTLGenerator, SimulatorGenerator, ManifestGenerator

class TestVFPGAEngine(unittest.TestCase):
    def setUp(self):
        self.sample_dts = """
        / {
            compatible = "test,device-a", "test,device-b";
            model = "Test Board Model V1";
            vfpga_reg@40000000 {
                compatible = "generic-uio";
                reg = <0x40000000 0x1000>;
                label = "/dev/uio0";
                registers = "EN@0x14, RST@0x10";
            };
            i2c_test@50000000 {
                compatible = "cdns,i2c-r1p10";
                reg = <0x50000000 0x100>;
                label = "/dev/i2c-1";
                bus_id = "1";
            };
        };
        """
        # テンポラリなDTSファイルを作成
        with open("test_sample.dts", "w") as f:
            f.write(self.sample_dts)

    def tearDown(self):
        if os.path.exists("test_sample.dts"):
            os.remove("test_sample.dts")

    def test_parser(self):
        model = DTSParser.parse("test_sample.dts")
        self.assertEqual(len(model.devices), 2)
        
        # compatible と model 名のパース結果を確認 (カンマ区切り文字列のパース検証)
        self.assertEqual(model.compatible_bytes, b"test,device-a\x00test,device-b\x00")
        self.assertEqual(model.model_name, "Test Board Model V1")
        
        uio = model.get_uio_device()
        self.assertIsNotNone(uio)
        self.assertEqual(uio.name, "vfpga_reg")
        self.assertEqual(uio.path, "/dev/uio0")
        
        # レジスタが正しくパースされているか
        reg_names = [r.name for r in uio.registers]
        self.assertIn("EN", reg_names)
        self.assertIn("RST", reg_names)
        
        # I2C デバイスの確認
        i2c = next(d for d in model.devices if d.type == "i2c")
        self.assertEqual(i2c.extra_props["bus_id"], "1")

    def test_config_generator(self):
        model = DTSParser.parse("test_sample.dts")
        gen = SystemConfigGenerator()
        content = gen.generate(model)
        self.assertIn("#define SHM_FILE \"/tmp/vfpga_reg\"", content)
        self.assertIn("#define SHM_SIZE 4096", content)

    def test_shim_generator(self):
        model = DTSParser.parse("test_sample.dts")
        gen = ShimGenerator()
        content = gen.generate(model)
        # MMAP ルートの確認
        self.assertIn("{ 0x40000000, 0x1000, SHM_FILE, \"/dev/uio0\" }", content)
        # I2C マッチングの確認
        self.assertIn("strcmp(pathname, \"/dev/i2c-1\") == 0", content)
        # 基本的な関数の存在確認
        self.assertIn("int open(const char *pathname, int flags, ...)", content)
        self.assertIn("void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)", content)

    def test_rtl_generator(self):
        model = DTSParser.parse("test_sample.dts")
        gen = RTLGenerator()
        content = gen.generate(model)
        self.assertIn("module vfpga_top", content)
        self.assertIn("output reg [31:0] EN", content)
        self.assertIn("output reg [31:0] RST", content)
        self.assertIn("32'h40000014: EN <= w_data;", content)

    def test_manifest_generator(self):
        import json
        model = DTSParser.parse("test_sample.dts")
        gen = ManifestGenerator()
        content = gen.generate(model)
        manifest = json.loads(content)
        self.assertEqual(manifest["board"], "vfpga_reg")
        self.assertEqual(manifest["shm_path"], "/tmp/vfpga_reg")
        self.assertEqual(manifest["hdmi_output_path"], "/tmp/hdmi_output.bmp")
        self.assertTrue(any(d["name"] == "vfpga_reg" for d in manifest["devices"]))
        self.assertTrue(any(d["type"] == "i2c" for d in manifest["devices"]))

if __name__ == "__main__":
    unittest.main()
