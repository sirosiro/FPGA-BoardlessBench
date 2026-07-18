import os
from vfpga.models import BoardModel
from vfpga.generator_base import BaseGenerator

class ShimGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        mmap_routes, i2c_matches, uart_matches, spi_matches, rpmsg_matches = [], [], [], [], []
        uart_count = 0
        for i, dev in enumerate(model.devices):
            if dev.type in ['uio', 'gpio']:
                reg_parts = dev.base_reg.split()
                if len(reg_parts) >= 2:
                    mmap_routes.append('    { %s, %s, SHM_FILE, "%s" }' % (reg_parts[0], reg_parts[1], dev.path))
            elif dev.type == 'i2c':
                bus_id = dev.extra_props.get('bus_id', '1')
                i2c_matches.append('    if (pathname != NULL && strcmp(pathname, "%s") == 0) return %d;' % (dev.path, int(bus_id) + 1))
            elif dev.type == 'uart':
                uart_count += 1
                uart_matches.append('    if (pathname != NULL && strcmp(pathname, "%s") == 0) return %d;' % (dev.path, uart_count))
            elif dev.type == 'spi':
                bus_id = dev.extra_props.get('bus_id', '0')
                if hasattr(dev, 'spi_slaves'):
                    for slave in dev.spi_slaves:
                        dev_path = f"/dev/spidev{bus_id}.{slave.cs}"
                        spi_code = (int(bus_id) << 8) | int(slave.cs)
                        spi_matches.append('    if (pathname != NULL && strcmp(pathname, "%s") == 0) return 0x1%04X;' % (dev_path, spi_code))
            elif dev.type == 'rpmsg':
                rpmsg_matches.append(
                    '    if (pathname != NULL && strcmp(pathname, "%s") == 0) {\n'
                    '        return handle_rpmsg_open();\n'
                    '    }' % dev.path
                )
        
        # テンプレートファイルを読み込む
        template_path = os.path.join(os.path.dirname(__file__), 'templates/libfpgashim.c.template')
        with open(template_path, 'r') as f:
            template = f.read()

        return template % (
            ", ".join(mmap_routes),
            " ".join(i2c_matches),
            " ".join(uart_matches),
            " ".join(spi_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches)
        )
