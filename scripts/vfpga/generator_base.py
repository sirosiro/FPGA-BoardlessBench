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
            if dev.type == "spi" and dev.base_addr != 0xE0006000 and dev.base_addr != 0xE0007000:
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
            SoC/ベンダー非依存の汎用クラス名マクロ（FBB_DEV_PATH_SERIAL, FBB_DEV_PATH_I2C 等）に加え、
            ノードラベル固有のマクロ（FBB_DEV_PATH_UART0, FBB_DEV_PATH_I2C0 等）もエイリアスとして出力し、
            既存ファームウェアとの 100% の後方互換性を保証する。
        """
        dev_path_macros = []
        defined_macro_names = set()

        def add_macro(macro_name, path_val, add_zero_alias=False):
            if not macro_name or macro_name in defined_macro_names:
                return
            defined_macro_names.add(macro_name)
            dev_path_macros.append(f'#ifndef {macro_name}\n#define {macro_name} "{path_val}"\n#endif')
            if add_zero_alias and not macro_name.endswith("_0"):
                zero_alias = f"{macro_name}_0"
                if zero_alias not in defined_macro_names:
                    defined_macro_names.add(zero_alias)
                    dev_path_macros.append(f"#ifndef {zero_alias}\n#define {zero_alias} {macro_name}\n#endif")

        # 1. 汎用ノード名（node_name）に基づく標準マクロ生成（後方互換性・標準API）
        # 同一ノード名が複数ある場合は name, name_1, name_2 ... としてナンバリング
        generic_names = []
        counts = {}
        for dev in model.devices:
            base_g = (getattr(dev, "node_name", None) or dev.name.split("_")[0]).lower()
            if base_g in counts:
                g_numbered = f"{base_g}_{counts[base_g]}"
                counts[base_g] += 1
            else:
                g_numbered = base_g
                counts[base_g] = 1
            generic_names.append(g_numbered)

        for dev, g_name in zip(model.devices, generic_names):
            clean_name = "".join(c if c.isalnum() else "_" for c in g_name).upper()
            add_macro(f"FBB_DEV_PATH_{clean_name}", dev.path, add_zero_alias=True)

        # 2. デバイス固有名 / ラベル（dev.name / dev.label）に基づく個別マクロ・エイリアス生成
        for dev in model.devices:
            for name_candidate in [dev.name, getattr(dev, "label", None)]:
                if not name_candidate:
                    continue
                clean_name = "".join(c if c.isalnum() else "_" for c in name_candidate).upper()
                add_macro(f"FBB_DEV_PATH_{clean_name}", dev.path, add_zero_alias=True)

        dev_path_macros_str = "\n".join(dev_path_macros)

        return """/* Auto-generated Device Config from DTS */
#ifndef VFPGA_DEVICE_CONFIG_H
#define VFPGA_DEVICE_CONFIG_H

/* Device Paths */
%s
#endif
""" % dev_path_macros_str
