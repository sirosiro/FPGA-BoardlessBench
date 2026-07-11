# =============================================================================
# Data Models
# =============================================================================

class Register:
    def __init__(self, name, offset, direction='RW', logical_name=None):
        self.name = name
        self.offset = offset
        self.direction = direction.upper()
        self.logical_name = logical_name

class I2CSlave:
    def __init__(self, name, addr, compatible, mock_file=None, init_val=0x10):
        self.name = name
        self.addr = addr
        self.compatible = compatible
        self.mock_file = mock_file
        self.init_val = init_val

class SPISlave:
    def __init__(self, name, cs, compatible, mock_file=None, init_val=2048):
        self.name = name
        self.cs = cs
        self.compatible = compatible
        self.mock_file = mock_file
        self.init_val = init_val

class Device:
    def __init__(self, name, path, dev_type, base_reg):
        self.name = name
        self.path = path
        self.type = dev_type
        self.base_reg = base_reg
        self.registers = []
        self.extra_props = {}
        self.i2c_slaves = []
        self.spi_slaves = []
        # Parse base_addr and size from base_reg (e.g. "0x40000000 0x1000")
        try:
            parts = base_reg.split()
            self.base_addr = int(parts[0], 0) if len(parts) >= 1 else 0
            self.size = int(parts[1], 0) if len(parts) >= 2 else 0
        except:
            self.base_addr = 0
            self.size = 0

class BoardModel:
    def __init__(self, devices, name="vfpga"):
        self.devices = devices
        self.name = name
    def get_uio_device(self):
        return next((d for d in self.devices if d.type in ['uio', 'gpio']), None)
    def get_uio_devices(self):
        return [d for d in self.devices if d.type in ['uio', 'gpio']]
    def get_uart_devices(self):
        return [d for d in self.devices if d.type == 'uart']
