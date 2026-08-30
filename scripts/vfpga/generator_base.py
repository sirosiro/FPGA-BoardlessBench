"""
@file scripts/vfpga/generator_base.py
@intent:responsibility
    DTS パース結果（BoardModel）を受け取り、各種 C/C++ ヘッダーファイル（vfpga_system_config.h, vfpga_device_config.h）を生成するジェネレータ基底クラスおよび実装。
@intent:rationale
    F-BB ではインフラ層設定（シミュレータ/Shim用）とファームウェア層定義（デバイスパス）を異なるヘッダーに完全分離することで、
    FW コードにホストビルドマシンの絶対パスや環境依存情報が混入することを防ぎ、実機への 100% 可搬性を保証する（ADR #006）。
"""

import os
from vfpga.models import BoardModel


class BaseGenerator:
    """
    @class BaseGenerator
    @intent:responsibility
        すべてのコードジェネレータ（Shim, RTL, Simulator, Config）の共通インターフェースを定義。
    """

    def generate(self, model: BoardModel):
        raise NotImplementedError


class SystemConfigGenerator(BaseGenerator):
    """
    @class SystemConfigGenerator
    @intent:responsibility
        DTS モデルから vfpga_system_config.h を生成し、共有メモリサイズ・ファイル名・物理ピンソケットパスを出力。
    @intent:rationale
        シミュレータコアと C-Shim が共有する低レイヤー内部パラメータを一元管理する。
    """

    @staticmethod
    def compute_shm_size(model: BoardModel):
        """
        @intent:responsibility
            全 UIO/GPIO デバイスの物理アドレス範囲をカバーする最小十分な共有メモリ（SHM）サイズを計算する。
        @intent:rationale
            複数デバイスのアドレス空間を単一の共有メモリで連続カバーし、アドレス変換オーバーヘッドを削減する。
        """
        devs = model.get_uio_devices()
        if not devs:
            return 1024
        if len(devs) == 1:
            return devs[0].size
        # 複数デバイスの場合: 最小ベースアドレスから最大終端アドレスまでカバー
        min_addr = min(d.base_addr for d in devs)
        max_end = max(d.base_addr + d.size for d in devs)
        return max_end - min_addr

    def generate(self, model: BoardModel):
        """
        @intent:responsibility
            vfpga_system_config.h のソースコード文字列を生成する。
        @intent:pre-condition
            model に有効なデバイス一覧および名前が設定されていること。
        """
        shm_name = model.name
        shm_size = self.compute_shm_size(model)

        # PL SPI デバイスの検出
        pl_spi_socket = ""
        for dev in model.devices:
            if dev.type == "spi" and dev.base_reg != 0xE0006000 and dev.base_reg != 0xE0007000:
                if hasattr(dev, "spi_slaves") and dev.spi_slaves:
                    slave = dev.spi_slaves[0]
                    bus_id = dev.extra_props.get("bus_id", 1)
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
""" % (
            shm_name,
            shm_name,
            shm_size,
            pl_spi_socket,
        )


class DeviceConfigGenerator(BaseGenerator):
    """
    @class DeviceConfigGenerator
    @intent:responsibility
        DTS モデルから vfpga_device_config.h を生成し、ファームウェア用の論理デバイスパスマクロを出力。
    @intent:rationale
        ファームウェアコードが /dev/uio0 や /dev/spidev0.0 などの実機標準パスを透過的に参照できるようにマクロを提供。
        実機ビルド時には本ヘッダーを無視または実機定義で置き換えるだけでクロスコンパイルが可能になる。
    """

    def generate(self, model: BoardModel):
        """
        @intent:responsibility
            重複を排除した論理デバイスパスマクロ（FBB_DEV_PATH_*）群を生成する。
        """
        # 各デバイスのパス用マクロ生成
        dev_path_macros = []
        name_counts = {}
        for dev in model.devices:
            clean_name = "".join(c if c.isalnum() else "_" for c in dev.name).upper()
            name_counts[clean_name] = name_counts.get(clean_name, 0) + 1

        current_indices = {}
        for dev in model.devices:
            clean_name = "".join(c if c.isalnum() else "_" for c in dev.name).upper()
            if name_counts[clean_name] > 1:
                idx = current_indices.get(clean_name, 0)
                current_indices[clean_name] = idx + 1
                macro_name = f"FBB_DEV_PATH_{clean_name}_{idx}"
            else:
                macro_name = f"FBB_DEV_PATH_{clean_name}"
            dev_path_macros.append(f'#ifndef {macro_name}\n#define {macro_name} "{dev.path}"\n#endif')
            if not macro_name.endswith("_0") and name_counts[clean_name] == 1:
                dev_path_macros.append(f"#ifndef {macro_name}_0\n#define {macro_name}_0 {macro_name}\n#endif")
        dev_path_macros_str = "\n".join(dev_path_macros)

        return """/* Auto-generated Device Config from DTS */
#ifndef VFPGA_DEVICE_CONFIG_H
#define VFPGA_DEVICE_CONFIG_H

/* Device Paths */
%s
#endif
""" % dev_path_macros_str
