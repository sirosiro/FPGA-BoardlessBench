"""
@file scripts/vfpga/generator_shim.py
@intent:responsibility
    DTS モデル（BoardModel）を解析し、libfpgashim.c.template に対する mmap ルートテーブル（routes[]）、
    レジスタアクセス権限テーブル（reg_perms[]）、I2C/UART/SPI/RPMsg 各デバイスパス判定コードを展開・注入して
    具象 C-Shim ソースコード（libfpgashim.c）を生成する。
@intent:rationale
    DTS の変更に合わせて C-Shim のルーティングテーブルを 100% 自動同期させることで、
    手動修正によるマッピング漏れやレジスタ権限の不整合を根絶する（Single Source of Truth 原則）。
"""

import os
from vfpga.models import BoardModel
from vfpga.generator_base import BaseGenerator


class ShimGenerator(BaseGenerator):
    """
    @class ShimGenerator
    @intent:responsibility
        libfpgashim.c の生成を担当。DTS のペリフェラルノードから各種ルート情報・判定コードを展開。
    """

    def generate(self, model: BoardModel):
        """
        @intent:responsibility
            BoardModel のデバイス構成から libfpgashim.c ソースコード文字列を生成する。
        @intent:pre-condition
            templates/libfpgashim.c.template が存在し、必要なフォーマット指定子（%s / %d）を含んでいること。
        """
        mmap_routes, i2c_matches, uart_matches, spi_matches, rpmsg_matches = [], [], [], [], []
        uart_count = 0
        mmap_devs = [dev for dev in model.devices if dev.type in ["uio", "gpio", "dma"]]
        mmap_devs.sort(key=lambda d: d.base_addr)
        for dev in mmap_devs:
            reg_parts = dev.base_reg.split()
            if len(reg_parts) >= 2:
                mmap_routes.append('    { %s, %s, SHM_FILE, "%s" }' % (reg_parts[0], reg_parts[1], dev.path))
        for i, dev in enumerate(model.devices):
            if dev.type == "i2c":
                bus_id = dev.extra_props.get("bus_id", "1")
                i2c_matches.append(
                    '    if (pathname != NULL && strcmp(pathname, "%s") == 0) { if (out_bus_id) *out_bus_id = %s; return 1; }'
                    % (dev.path, bus_id)
                )
            elif dev.type == "uart":
                uart_count += 1
                uart_matches.append(
                    '    if (pathname != NULL && strcmp(pathname, "%s") == 0) { if (out_uart_id) *out_uart_id = %d; return 1; }'
                    % (dev.path, uart_count)
                )
            elif dev.type == "spi":
                bus_id = dev.extra_props.get("bus_id", "0")
                if hasattr(dev, "spi_slaves"):
                    for slave in dev.spi_slaves:
                        dev_path = f"/dev/spidev{bus_id}.{slave.cs}"
                        spi_code = (int(bus_id) << 8) | int(slave.cs)
                        spi_matches.append(
                            '    if (pathname != NULL && strcmp(pathname, "%s") == 0) { if (out_spi_code) *out_spi_code = %d; return 1; }'
                            % (dev_path, spi_code)
                        )
            elif dev.type == "rpmsg":
                rpmsg_matches.append(
                    '    if (pathname != NULL && strcmp(pathname, "%s") == 0) {\n'
                    "        return handle_rpmsg_open();\n"
                    "    }" % dev.path
                )

        reg_perm_entries = []
        for dev in mmap_devs:
            for reg in dev.registers:
                base_addr_str = dev.base_reg.split()[0] if dev.base_reg else "0x0"
                reg_perm_entries.append('    { %s, %s, "%s", "%s" }' % (base_addr_str, reg.offset, reg.name, reg.direction))
        reg_perm_str = ", ".join(reg_perm_entries) if reg_perm_entries else ""
        num_reg_perms = len(reg_perm_entries)

        # テンプレートファイルを読み込む
        template_path = os.path.join(os.path.dirname(__file__), "templates/libfpgashim.c.template")
        with open(template_path, "r") as f:
            template = f.read()

        return template % (
            ", ".join(mmap_routes),
            reg_perm_str,
            num_reg_perms,
            " ".join(i2c_matches),
            " ".join(uart_matches),
            " ".join(spi_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches),
        )
