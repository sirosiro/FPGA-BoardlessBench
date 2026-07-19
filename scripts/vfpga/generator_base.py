import os
from vfpga.models import BoardModel

class BaseGenerator:
    def generate(self, model: BoardModel):
        raise NotImplementedError

class SystemConfigGenerator(BaseGenerator):
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
        
        # PL SPI デバイスの検出
        pl_spi_socket = ""
        for dev in model.devices:
            if dev.type == 'spi' and dev.base_reg != 0xe0006000 and dev.base_reg != 0xe0007000:
                if hasattr(dev, 'spi_slaves') and dev.spi_slaves:
                    slave = dev.spi_slaves[0]
                    bus_id = dev.extra_props.get('bus_id', 1)
                    cs = slave.cs
                    pl_spi_socket = f"/tmp/fbb_spi_b{bus_id}_c{cs}"
                    break
        
        return """/* Auto-generated System Config from DTS */
#ifndef VFPGA_SYSTEM_CONFIG_H
#define VFPGA_SYSTEM_CONFIG_H
#define SHM_NAME "%s"
#define SHM_FILE "/tmp/%s"
#define SHM_SIZE %d
#define GPIO_COUNT 118
#define PL_SPI_SOCKET "%s"
#endif
""" % (shm_name, shm_name, shm_size, pl_spi_socket)

class DeviceConfigGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        # 各デバイスのパス用マクロ生成
        dev_path_macros = []
        for dev in model.devices:
            # マクロ名として安全な大文字表記にクリーンアップ
            clean_name = "".join(c if c.isalnum() else "_" for c in dev.name).upper()
            macro_name = f"FBB_DEV_PATH_{clean_name}"
            dev_path_macros.append(f'#ifndef {macro_name}\n#define {macro_name} "{dev.path}"\n#endif')
        dev_path_macros_str = "\n".join(dev_path_macros)
        
        return """/* Auto-generated Device Config from DTS */
#ifndef VFPGA_DEVICE_CONFIG_H
#define VFPGA_DEVICE_CONFIG_H

/* Device Paths */
%s
#endif
""" % dev_path_macros_str
