import os
import sys
from vfpga.models import BoardModel
from vfpga.generator_base import SystemConfigGenerator, DeviceConfigGenerator
from vfpga.generator_shim import ShimGenerator
from vfpga.generator_rtl import RTLGenerator, SimulatorGenerator, ManifestGenerator, RustPACGenerator
from vfpga.generator_gdb import GdbExtensionGenerator

class GeneratorOrchestrator:
    def __init__(self, model: BoardModel, dts_path: str = None):
        self.model = model
        self.dts_path = dts_path
        # プロジェクトルートを取得 (vfpga/orchestrator.py から見て 2つ上の階層)
        self.project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
        self.generators = {
            "src/include/vfpga_system_config.h": SystemConfigGenerator(),
            "vfpga_device_config.h": DeviceConfigGenerator(),
            "src/shim/libfpgashim.c": ShimGenerator(),
            "src/rtl/vfpga_top.v": RTLGenerator(),
            "src/sim/sim_main.cpp": SimulatorGenerator(),
            "dashboard/data/board_manifest.json": ManifestGenerator()
        }

    def generate_all(self):
        dts_dir = os.path.dirname(os.path.abspath(self.dts_path)) if self.dts_path else None
        if dts_dir:
            self.model.scenario_dir = dts_dir
        
        for rel_path, gen in self.generators.items():
            content = gen.generate(self.model)
            if rel_path == "vfpga_device_config.h":
                abs_path = os.path.join(dts_dir, "vfpga_device_config.h") if dts_dir else os.path.join(self.project_root, "src/include/vfpga_device_config.h")
            else:
                abs_path = os.path.join(self.project_root, rel_path)
            
            dir_name = os.path.dirname(abs_path)
            if dir_name:
                os.makedirs(dir_name, exist_ok=True)
            with open(abs_path, "w") as f:
                f.write(content)
        
        # Check if there is a .rs file in the directory of the DTS file
        if self.dts_path:
            dts_dir = os.path.dirname(os.path.abspath(self.dts_path))
            if os.path.exists(dts_dir):
                rs_files = [f for f in os.listdir(dts_dir) if f.endswith('.rs')]
                if rs_files:
                    pac_content = RustPACGenerator().generate(self.model)
                    pac_path = os.path.join(dts_dir, "fbb_pac.rs")
                    with open(pac_path, "w") as f:
                        f.write(pac_content)

                # Generate fbb_gdb.py and .gdbinit for interactive and IDE GDB debugging
                gdb_content = GdbExtensionGenerator().generate(self.model)
                gdb_path = os.path.join(dts_dir, "fbb_gdb.py")
                with open(gdb_path, "w", encoding="utf-8") as f:
                    f.write(gdb_content)

                gdbinit_path = os.path.join(dts_dir, ".gdbinit")
                gdbinit_content = """# Auto-generated .gdbinit for F-BB Scenario
set pagination off
set print pretty on
python
import os, sys
scenario_dir = os.path.dirname(os.path.abspath(gdb.current_progspace().filename)) if (hasattr(gdb, 'current_progspace') and gdb.current_progspace() and gdb.current_progspace().filename) else "."
gdb_script = os.path.join(scenario_dir, "fbb_gdb.py")
if os.path.exists(gdb_script):
    gdb.execute(f"source {gdb_script}")
end
"""
                with open(gdbinit_path, "w", encoding="utf-8") as f:
                    f.write(gdbinit_content)
        
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
