import os
from vfpga.models import BoardModel

class BaseGenerator:
    def generate(self, model: BoardModel):
        raise NotImplementedError

class ConfigGenerator(BaseGenerator):
    @staticmethod
    def compute_shm_size(model: BoardModel):
        """全UIO/GPIOデバイスの物理アドレス範囲をカバーするSHMサイズを計算"""
        devs = model.get_uio_devices()
        if not devs:
            return 1024
        if len(devs) == 1:
            return devs[0].size
        # 複数デバイスの場合: 最小ベースアドレスから最大終端アドレスまでカバー
        min_addr = min(d.base_addr for d in devs)
        max_end  = max(d.base_addr + d.size for d in devs)
        return max_end - min_addr

    def generate(self, model: BoardModel):
        shm_name = model.name
        shm_size = self.compute_shm_size(model)
        # プロジェクトルートを動的に取得 (vfpga/generator_base.py から見て 2つ上の階層)
        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
        scenario_dir = getattr(model, "scenario_dir", "")
        
        return """/* Auto-generated Config from DTS */
#ifndef VFPGA_CONFIG_H
#define VFPGA_CONFIG_H
#define PROJECT_ROOT "%s"
#define SCENARIO_DIR "%s"
#define SHM_NAME "%s"
#define SHM_FILE "/tmp/%s"
#define SHM_SIZE %d
#define GPIO_COUNT 118
#endif
""" % (project_root, scenario_dir, shm_name, shm_name, shm_size)
