import os
import sys
from vfpga.models import BoardModel
from vfpga.generator_base import ConfigGenerator
from vfpga.generator_shim import ShimGenerator
from vfpga.generator_rtl import RTLGenerator, SimulatorGenerator, ManifestGenerator, RustPACGenerator

class GeneratorOrchestrator:
    def __init__(self, model: BoardModel, dts_path: str = None):
        self.model = model
        self.dts_path = dts_path
        # プロジェクトルートを取得 (vfpga/orchestrator.py から見て 2つ上の階層)
        self.project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
        self.generators = {
            "src/include/vfpga_config.h": ConfigGenerator(),
            "src/shim/libfpgashim.c": ShimGenerator(),
            "src/rtl/vfpga_top.v": RTLGenerator(),
            "src/sim/sim_main.cpp": SimulatorGenerator(),
            "dashboard/data/board_manifest.json": ManifestGenerator()
        }

    def generate_all(self):
        for rel_path, gen in self.generators.items():
            content = gen.generate(self.model)
            # 絶対パスを構築
            abs_path = os.path.join(self.project_root, rel_path)
            dir_name = os.path.dirname(abs_path)
            if dir_name:
                os.makedirs(dir_name, exist_ok=True)
            with open(abs_path, "w") as f:
                f.write(content)
        
        # Check if there is a .rs file in the directory of the DTS file
        if self.dts_path:
            dts_dir = os.path.dirname(self.dts_path)
            if os.path.exists(dts_dir):
                rs_files = [f for f in os.listdir(dts_dir) if f.endswith('.rs')]
                if rs_files:
                    pac_content = RustPACGenerator().generate(self.model)
                    pac_path = os.path.join(dts_dir, "fbb_pac.rs")
                    with open(pac_path, "w") as f:
                        f.write(pac_content)
        
        # /tmp/fbb_compatible を生成
        compatible_path = "/tmp/fbb_compatible"
        compatible_bytes = b"generic,fbb-vfpga\x00"
        if hasattr(self.model, "compatible_bytes"):
            compatible_bytes = self.model.compatible_bytes

        try:
            with open(compatible_path, "wb") as f:
                f.write(compatible_bytes)
        except Exception as e:
            print(f"[Warning] Failed to write {compatible_path}: {e}", file=sys.stderr)

        # /tmp/fbb_model を生成
        model_path = "/tmp/fbb_model"
        model_bytes = b"generic-vfpga\x00"
        if hasattr(self.model, "model_name"):
            model_bytes = self.model.model_name.encode('utf-8') + b"\x00"

        try:
            with open(model_path, "wb") as f:
                f.write(model_bytes)
        except Exception as e:
            print(f"[Warning] Failed to write {model_path}: {e}", file=sys.stderr)
