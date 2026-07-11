#!/usr/bin/env python3
import re
import os
import sys

# =============================================================================
# 1. Data Models
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

class Device:
    def __init__(self, name, path, dev_type, base_reg):
        self.name = name
        self.path = path
        self.type = dev_type
        self.base_reg = base_reg
        self.registers = []
        self.extra_props = {}
        self.i2c_slaves = []
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

# =============================================================================
# 2. Parser
# =============================================================================

class DTSParser:
    @staticmethod
    def find_matching_braces(text, start_pos):
        brace_pos = text.find('{', start_pos)
        if brace_pos == -1:
            return -1, -1
        count = 1
        i = brace_pos + 1
        while i < len(text) and count > 0:
            if text[i] == '{':
                count += 1
            elif text[i] == '}':
                count -= 1
            i += 1
        if count == 0:
            return brace_pos, i
        return -1, -1

    @staticmethod
    def parse(dts_path):
        with open(dts_path, 'r') as f:
            content = f.read()
        devices = []
        
        # 1. Look for root node '/'
        root_match = re.search(r'/\s*\{', content)
        if root_match:
            brace_start, brace_end = DTSParser.find_matching_braces(content, root_match.start())
            if brace_start != -1:
                content = content[brace_start + 1 : brace_end - 1]
                
        pos = 0
        while True:
            # Match top-level nodes (e.g. node@1000 or label: node@1000)
            match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', content[pos:])
            if not match:
                break
            match_start = pos + match.start()
            raw_name = match.group(1).strip()
            
            brace_start, brace_end = DTSParser.find_matching_braces(content, match_start)
            if brace_start == -1:
                pos = match_start + len(raw_name) + 1
                continue
            
            body = content[brace_start + 1 : brace_end - 1]
            pos = brace_end
            
            # Extract node name after label if colon exists
            node_name = raw_name
            if ':' in raw_name:
                node_name = raw_name.split(':')[-1].strip()
                
            name = node_name.split('@')[0]

            # Clean body by removing nested sub-node blocks to prevent property overriding
            clean_body = body
            while True:
                sub_match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', clean_body)
                if not sub_match:
                    break
                sub_start = sub_match.start()
                sub_brace_start, sub_brace_end = DTSParser.find_matching_braces(clean_body, sub_start)
                if sub_brace_start == -1:
                    break
                clean_body = clean_body[:sub_start] + clean_body[sub_brace_end:]

            props = {}
            prop_matches = re.finditer(r'([a-zA-Z0-9_-]+)\s*=\s*([^;]+);', clean_body)
            for p_match in prop_matches:
                k = p_match.group(1).strip()
                v = p_match.group(2).strip()
                if '{' in v or '}' in v:
                    continue
                if v.startswith('<') and v.endswith('>'): v = v[1:-1].strip()
                if v.startswith('"') and v.endswith('"'): v = v[1:-1].strip()
                props[k] = v
                
            if 'compatible' in props:
                compatible = props.get('compatible', '')
                label = props.get('label', "/dev/%s" % name)
                dev_type = 'unknown'
                if 'generic-uio' in compatible: dev_type = 'uio'
                elif 'i2c' in compatible or 'cdns,i2c' in compatible: dev_type = 'i2c'
                elif 'uart' in compatible or 'xlnx,xps-uart' in compatible: dev_type = 'uart'
                elif 'gpio' in compatible or 'xlnx,xps-gpio' in compatible: dev_type = 'gpio'
                elif 'rpmsg' in compatible: dev_type = 'rpmsg'
                if dev_type == 'unknown' and label.startswith('/dev/uio'):
                    dev_type = 'uio'
                
                device = Device(name, label, dev_type, props.get('reg', '0x0 0x0'))
                for k, v in props.items():
                    if k not in ['label', 'compatible', 'reg', 'registers']: device.extra_props[k] = v
                
                # Parse nested I2C slave devices
                if dev_type == 'i2c':
                    sub_pos = 0
                    while True:
                        sub_match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', body[sub_pos:])
                        if not sub_match:
                            break
                        sub_match_start = sub_pos + sub_match.start()
                        sub_raw_name = sub_match.group(1).strip()
                        
                        sub_brace_start, sub_brace_end = DTSParser.find_matching_braces(body, sub_match_start)
                        if sub_brace_start == -1:
                            sub_pos = sub_match_start + len(sub_raw_name) + 1
                            continue
                        
                        sub_body = body[sub_brace_start + 1 : sub_brace_end - 1]
                        sub_pos = sub_brace_end
                        
                        s_node_name = sub_raw_name
                        if ':' in sub_raw_name:
                            s_node_name = sub_raw_name.split(':')[-1].strip()
                            
                        s_name = s_node_name.split('@')[0]
                        s_addr_str = s_node_name.split('@')[1] if '@' in s_node_name else "0"
                        try:
                            s_addr = int(s_addr_str, 16)
                        except:
                            s_addr = 0
                            
                        s_props = {}
                        s_prop_matches = re.finditer(r'([a-zA-Z0-9_-]+)\s*=\s*([^;]+);', sub_body)
                        for sp_match in s_prop_matches:
                            sk = sp_match.group(1).strip()
                            sv = sp_match.group(2).strip()
                            if sv.startswith('<') and sv.endswith('>'): sv = sv[1:-1].strip()
                            if sv.startswith('"') and sv.endswith('"'): sv = sv[1:-1].strip()
                            s_props[sk] = sv
                        
                        if 'compatible' in s_props:
                            init_val_str = s_props.get('fbb,mock-data', '0x10')
                            try:
                                init_val = int(init_val_str, 0)
                            except:
                                init_val = 0x10
                            mock_file = s_props.get('fbb,mock-file', None)
                            slave = I2CSlave(s_name, s_addr, s_props['compatible'], mock_file, init_val)
                            device.i2c_slaves.append(slave)
                
                if 'registers' in props:
                    reg_raw = props['registers'].replace('\\n', ' ').replace('\\"', '').replace('\\t', ' ')
                    reg_list = reg_raw.split(',')
                    for r_str in reg_list:
                        r_str = r_str.strip().strip('"').strip()
                        if '@' in r_str:
                            reg_parts = r_str.split('@')
                            reg_name = reg_parts[0].strip()
                            reg_offset = reg_parts[1].strip()
                            
                            logical_name = None
                            paren_match = re.match(r'^([a-zA-Z0-9_]+)\s*\(\s*([a-zA-Z0-9_]+)\s*\)$', reg_name)
                            if paren_match:
                                reg_name = paren_match.group(1)
                                logical_name = paren_match.group(2)
                            else:
                                logical_name = reg_name
                            
                            device.registers.append(Register(reg_name, reg_offset, 'RW', logical_name))
                devices.append(device)
        
        # 共有メモリ名として使用するボード名を決定（UIO > GPIO > デフォルト）
        board_name = "vfpga_reg"
        uio = next((d for d in devices if d.type == 'uio'), None)
        if uio:
            board_name = uio.name
        else:
            gpio = next((d for d in devices if d.type == 'gpio'), None)
            if gpio: board_name = gpio.name
        
        # 最上位ノードの compatible を抽出してヌル文字区切りのバイト列にする
        compatible_bytes = b"generic,fbb-vfpga\x00"
        root_compat_match = re.search(r'/\s*\{[^{]*?compatible\s*=\s*([^;]+);', content, re.DOTALL)
        if root_compat_match:
            raw_compat = root_compat_match.group(1).strip()
            parts = [p.strip().strip('"').strip() for p in raw_compat.split(',')]
            compatible_bytes = b"".join([p.encode('utf-8') + b"\x00" for p in parts if p])

        # 最上位ノードの model を抽出
        model_name = "generic-vfpga"
        root_model_match = re.search(r'/\s*\{[^{]*?model\s*=\s*"([^"]+)";', content, re.DOTALL)
        if root_model_match:
            model_name = root_model_match.group(1).strip()

        model = BoardModel(devices, name=board_name)
        model.compatible_bytes = compatible_bytes
        model.model_name = model_name
        model.scenario_dir = os.path.dirname(dts_path)
        return model

# =============================================================================
# 3. Generators
# =============================================================================

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
        # プロジェクトルートを動的に取得
        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../"))
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

class ShimGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        mmap_routes, i2c_matches, uart_matches, rpmsg_matches = [], [], [], []
        uart_count = 0
        for i, dev in enumerate(model.devices):
            if dev.type in ['uio', 'gpio']:
                reg_parts = dev.base_reg.split()
                if len(reg_parts) >= 2:
                    mmap_routes.append('    { %s, %s, SHM_FILE, "%s" }' % (reg_parts[0], reg_parts[1], dev.path))
            elif dev.type == 'i2c':
                bus_id = dev.extra_props.get('bus_id', '1')
                i2c_matches.append('    if (pathname != NULL && strcmp(pathname, "%s") == 0) return %s;' % (dev.path, bus_id))
            elif dev.type == 'uart':
                uart_count += 1
                uart_matches.append('    if (pathname != NULL && strcmp(pathname, "%s") == 0) return %d;' % (dev.path, uart_count))
            elif dev.type == 'rpmsg':
                rpmsg_matches.append(
                    '    if (pathname != NULL && strcmp(pathname, "%s") == 0) {\n'
                    '        return handle_rpmsg_open();\n'
                    '    }' % dev.path
                )
        
        return """
#define _GNU_SOURCE
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "vfpga_config.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define MAX_FDS 1024
static int virtual_fd_route_idx[MAX_FDS] = {0};

static int (*original_open)(const char *pathname, int flags, mode_t mode) = NULL;
static int (*original_open64)(const char *pathname, int flags, mode_t mode) = NULL;
static int (*original_openat)(int dirfd, const char *pathname, int flags, mode_t mode) = NULL;
static int (*original_openat64)(int dirfd, const char *pathname, int flags, mode_t mode) = NULL;
static int (*original_ioctl)(int fd, unsigned long request, void *argp) = NULL;
static void* (*original_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset) = NULL;
static ssize_t (*original_write)(int fd, const void *buf, size_t count) = NULL;
static ssize_t (*original_read)(int fd, void *buf, size_t count) = NULL;

struct mmap_route { unsigned long base_addr; unsigned long size; const char *shm_path; const char *path; };
static struct mmap_route routes[] = { %s };

static int handle_rpmsg_open(void);

static int find_route_by_path(const char *pathname) {
    if (pathname == NULL) return 0;
    if (strcmp(pathname, "/dev/mem") == 0) return -1;
    for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++)
        if (strcmp(pathname, routes[i].path) == 0) return i + 1;
    return 0;
}

static int is_i2c_device(const char *pathname) { (void)pathname; %s return 0; }
static int is_uart_device(const char *pathname) { (void)pathname; %s return 0; }

static int transfer_i2c_msg_via_socket(int bus_id, struct i2c_msg *msg) {
    char socket_path[128];
    snprintf(socket_path, sizeof(socket_path), "/tmp/fbb_i2c_b%%d_a%%02x", bus_id, msg->addr);
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(sock_fd);
        if (msg->flags & I2C_M_RD) memset(msg->buf, 0, msg->len);
        return 0;
    }
    uint16_t msg_addr = msg->addr;
    uint16_t msg_flags = msg->flags;
    uint16_t msg_len = msg->len;
    if (send(sock_fd, &msg_addr, sizeof(msg_addr), 0) < 0 ||
        send(sock_fd, &msg_flags, sizeof(msg_flags), 0) < 0 ||
        send(sock_fd, &msg_len, sizeof(msg_len), 0) < 0) {
        close(sock_fd);
        return -1;
    }
    if (!(msg->flags & I2C_M_RD)) {
        if (send(sock_fd, msg->buf, msg->len, 0) < 0) {
            close(sock_fd);
            return -1;
        }
    } else {
        uint16_t received = 0;
        while (received < msg->len) {
            ssize_t r = recv(sock_fd, msg->buf + received, msg->len - received, 0);
            if (r <= 0) {
                close(sock_fd);
                return -1;
            }
            received += r;
        }
    }
    close(sock_fd);
    return 0;
}

static const char *redirect_firmware_path(const char *pathname) {
    if (pathname != NULL && strncmp(pathname, "/lib/firmware/", 14) == 0) {
        static char redirected_path[1024];
        mkdir("/tmp", 0777);
        mkdir("/tmp/fbb", 0777);
        mkdir("/tmp/fbb/lib", 0777);
        mkdir("/tmp/fbb/lib/firmware", 0777);
        snprintf(redirected_path, sizeof(redirected_path), "/tmp/fbb/lib/firmware/%%s", pathname + 14);
        return redirected_path;
    }
    return pathname;
}

static void ensure_dir(void) {
    mkdir("/tmp", 0777);
    mkdir("/tmp/fbb", 0777);
    mkdir("/tmp/fbb/sys", 0777);
    mkdir("/tmp/fbb/sys/class", 0777);
    mkdir("/tmp/fbb/sys/class/remoteproc", 0777);
    mkdir("/tmp/fbb/sys/class/remoteproc/remoteproc0", 0777);
}

int open(const char *pathname, int flags, ...) {
    mode_t mode = 0; if (flags & O_CREAT) { va_list arg; va_start(arg, flags); mode = (mode_t)va_arg(arg, int); va_end(arg); }
    if (!original_open) original_open = dlsym(RTLD_NEXT, "open");
    pathname = redirect_firmware_path(pathname);
    fprintf(stderr, "[Shim Debug] open: %%s\\n", pathname); fflush(stderr);
%s
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/firmware") == 0) {
        ensure_dir();
        return original_open("/tmp/fbb/sys/class/remoteproc/remoteproc0/firmware", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/state") == 0) {
        ensure_dir();
        return original_open("/tmp/fbb/sys/class/remoteproc/remoteproc0/state", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/compatible") == 0) {
        pathname = "/tmp/fbb_compatible";
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/model") == 0) {
        pathname = "/tmp/fbb_model";
    }
    int route_idx = find_route_by_path(pathname);
    if (route_idx != 0) {
        int fd = original_open("/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = route_idx;
        return fd;
    }
    int i2c_bus_id = is_i2c_device(pathname);
    if (i2c_bus_id != 0) {
        int fd = original_open("/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = -100 - i2c_bus_id;
        return fd;
    }
    int uart_id = is_uart_device(pathname);
    if (uart_id != 0) {
        int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
        if (master_fd != -1) {
            grantpt(master_fd); unlockpt(master_fd);
            char *slave_name = ptsname(master_fd);
            if (slave_name) {
                char map_path[512];
                snprintf(map_path, sizeof(map_path), "%%s/dashboard/data/vfpga_uart_%%d", PROJECT_ROOT, uart_id);
                int map_fd = original_open(map_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (map_fd != -1) {
                    write(map_fd, slave_name, strlen(slave_name));
                    close(map_fd);
                }
            }
            if (master_fd < MAX_FDS) virtual_fd_route_idx[master_fd] = -200;
        }
        return master_fd;
    }
    return original_open(pathname, flags, mode);
}

int open64(const char *pathname, int flags, ...) {
    mode_t mode = 0; if (flags & O_CREAT) { va_list arg; va_start(arg, flags); mode = (mode_t)va_arg(arg, int); va_end(arg); }
    if (!original_open64) original_open64 = dlsym(RTLD_NEXT, "open64");
    pathname = redirect_firmware_path(pathname);
%s
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/firmware") == 0) {
        ensure_dir();
        return original_open64("/tmp/fbb/sys/class/remoteproc/remoteproc0/firmware", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/state") == 0) {
        ensure_dir();
        return original_open64("/tmp/fbb/sys/class/remoteproc/remoteproc0/state", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/compatible") == 0) {
        pathname = "/tmp/fbb_compatible";
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/model") == 0) {
        pathname = "/tmp/fbb_model";
    }
    int route_idx = find_route_by_path(pathname);
    if (route_idx != 0) {
        int fd = original_open64("/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = route_idx;
        return fd;
    }
    int i2c_bus_id = is_i2c_device(pathname);
    if (i2c_bus_id != 0) {
        int fd = original_open64("/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = -100 - i2c_bus_id;
        return fd;
    }
    int uart_id = is_uart_device(pathname);
    if (uart_id != 0) {
        int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
        if (master_fd != -1) {
            grantpt(master_fd); unlockpt(master_fd);
            char *slave_name = ptsname(master_fd);
            if (slave_name) {
                char map_path[512];
                snprintf(map_path, sizeof(map_path), "%%s/dashboard/data/vfpga_uart_%%d", PROJECT_ROOT, uart_id);
                int map_fd = original_open64(map_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (map_fd != -1) {
                    write(map_fd, slave_name, strlen(slave_name));
                    close(map_fd);
                }
            }
            if (master_fd < MAX_FDS) virtual_fd_route_idx[master_fd] = -200;
        }
        return master_fd;
    }
    return original_open64(pathname, flags, mode);
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0; if (flags & O_CREAT) { va_list arg; va_start(arg, flags); mode = (mode_t)va_arg(arg, int); va_end(arg); }
    if (!original_openat) original_openat = dlsym(RTLD_NEXT, "openat");
    pathname = redirect_firmware_path(pathname);
%s
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/firmware") == 0) {
        ensure_dir();
        return original_openat(dirfd, "/tmp/fbb/sys/class/remoteproc/remoteproc0/firmware", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/state") == 0) {
        ensure_dir();
        return original_openat(dirfd, "/tmp/fbb/sys/class/remoteproc/remoteproc0/state", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/compatible") == 0) {
        pathname = "/tmp/fbb_compatible";
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/model") == 0) {
        pathname = "/tmp/fbb_model";
    }
    int route_idx = find_route_by_path(pathname);
    if (route_idx != 0) {
        int fd = original_openat(dirfd, "/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = route_idx;
        return fd;
    }
    int i2c_bus_id = is_i2c_device(pathname);
    if (i2c_bus_id != 0) {
        int fd = original_openat(dirfd, "/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = -100 - i2c_bus_id;
        return fd;
    }
    int uart_id = is_uart_device(pathname);
    if (uart_id != 0) {
        int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
        if (master_fd != -1) {
            grantpt(master_fd); unlockpt(master_fd);
            char *slave_name = ptsname(master_fd);
            if (slave_name) {
                char map_path[512];
                snprintf(map_path, sizeof(map_path), "%%s/dashboard/data/vfpga_uart_%%d", PROJECT_ROOT, uart_id);
                int map_fd = original_openat(AT_FDCWD, map_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (map_fd != -1) {
                    write(map_fd, slave_name, strlen(slave_name));
                    close(map_fd);
                }
            }
            if (master_fd < MAX_FDS) virtual_fd_route_idx[master_fd] = -200;
        }
        return master_fd;
    }
    return original_openat(dirfd, pathname, flags, mode);
}

int openat64(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0; if (flags & O_CREAT) { va_list arg; va_start(arg, flags); mode = (mode_t)va_arg(arg, int); va_end(arg); }
    if (!original_openat64) original_openat64 = dlsym(RTLD_NEXT, "openat64");
    pathname = redirect_firmware_path(pathname);
%s
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/firmware") == 0) {
        ensure_dir();
        return original_openat64(dirfd, "/tmp/fbb/sys/class/remoteproc/remoteproc0/firmware", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/class/remoteproc/remoteproc0/state") == 0) {
        ensure_dir();
        return original_openat64(dirfd, "/tmp/fbb/sys/class/remoteproc/remoteproc0/state", flags | O_CREAT, mode ? mode : 0666);
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/compatible") == 0) {
        pathname = "/tmp/fbb_compatible";
    }
    if (pathname != NULL && strcmp(pathname, "/sys/firmware/devicetree/base/model") == 0) {
        pathname = "/tmp/fbb_model";
    }
    int route_idx = find_route_by_path(pathname);
    if (route_idx != 0) {
        int fd = original_openat64(dirfd, "/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = route_idx;
        return fd;
    }
    int i2c_bus_id = is_i2c_device(pathname);
    if (i2c_bus_id != 0) {
        int fd = original_openat64(dirfd, "/dev/null", flags, mode);
        if (fd != -1 && fd < MAX_FDS) virtual_fd_route_idx[fd] = -100 - i2c_bus_id;
        return fd;
    }
    int uart_id = is_uart_device(pathname);
    if (uart_id != 0) {
        int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
        if (master_fd != -1) {
            grantpt(master_fd); unlockpt(master_fd);
            char *slave_name = ptsname(master_fd);
            if (slave_name) {
                char map_path[512];
                snprintf(map_path, sizeof(map_path), "%%s/dashboard/data/vfpga_uart_%%d", PROJECT_ROOT, uart_id);
                int map_fd = original_openat64(AT_FDCWD, map_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (map_fd != -1) {
                    write(map_fd, slave_name, strlen(slave_name));
                    close(map_fd);
                }
            }
            if (master_fd < MAX_FDS) virtual_fd_route_idx[master_fd] = -200;
        }
        return master_fd;
    }
    return original_openat64(dirfd, pathname, flags, mode);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!original_mmap) original_mmap = dlsym(RTLD_NEXT, "mmap");
    if (fd >= 0 && fd < MAX_FDS && virtual_fd_route_idx[fd] != 0) {
        int route_idx = virtual_fd_route_idx[fd];
        int target_idx = -1;
        if (route_idx == -1) {
            for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++) {
                if ((unsigned long)offset >= routes[i].base_addr && (unsigned long)offset < (routes[i].base_addr + routes[i].size)) {
                    target_idx = i; offset = (off_t)(routes[i].base_addr - routes[0].base_addr); break;
                }
            }
        } else if (route_idx > 0) {
            target_idx = route_idx - 1;
            offset = (off_t)(routes[target_idx].base_addr - routes[0].base_addr);
        }
        
        if (target_idx != -1) {
            int shm_fd = original_open(routes[target_idx].shm_path, O_RDWR, 0666);
            if (shm_fd < 0) {
                fprintf(stderr, "[Shim] ERROR: Failed to open %%s! (errno: %%d)\\n", routes[target_idx].shm_path, errno);
            } else {
                void *res = original_mmap(addr, length, prot, flags, shm_fd, offset);
                if (res == MAP_FAILED) {
                    fprintf(stderr, "[Shim] ERROR: original_mmap failed! (shm_fd: %%d, length: %%zu, offset: %%ld, errno: %%d)\\n", shm_fd, length, (long)offset, errno);
                }
                close(shm_fd); 
                return res;
            }
        }
    }
    return original_mmap(addr, length, prot, flags, fd, offset);
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args; va_start(args, request); void *argp = va_arg(args, void *); va_end(args);
    if (!original_ioctl) original_ioctl = dlsym(RTLD_NEXT, "ioctl");
    if (fd >= 0 && fd < MAX_FDS && virtual_fd_route_idx[fd] == -300) {
        return 0; // RPMsg ioctl dummy success
    }
    if (fd >= 0 && fd < MAX_FDS && virtual_fd_route_idx[fd] <= -101) {
        int i2c_bus_id = -(virtual_fd_route_idx[fd] + 100);
        if (request == I2C_RDWR) {
            struct i2c_rdwr_ioctl_data *data = (struct i2c_rdwr_ioctl_data *)argp;
            int ret = 0;
            for (unsigned int i = 0; i < data->nmsgs; i++) {
                if (transfer_i2c_msg_via_socket(i2c_bus_id, &data->msgs[i]) < 0) {
                    ret = -1;
                }
            }
            return ret;
        }
        if (request == I2C_SLAVE || request == I2C_SLAVE_FORCE) return 0;
    }
    return original_ioctl(fd, request, argp);
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (!original_write) original_write = dlsym(RTLD_NEXT, "write");
    return original_write(fd, buf, count);
}

ssize_t read(int fd, void *buf, size_t count) {
    if (!original_read) original_read = dlsym(RTLD_NEXT, "read");
    return original_read(fd, buf, count);
}

void fbb_ipi_notify(int dest_cpu) {
    if (!original_open) original_open = dlsym(RTLD_NEXT, "open");
    char path[256];
    if (dest_cpu == 0) {
        strcpy(path, "/tmp/fbb/sys/class/remoteproc/remoteproc0/proxy_pid");
    } else {
        strcpy(path, "/tmp/fbb/sys/class/remoteproc/remoteproc0/pid");
    }
    
    int fd = original_open(path, O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "[Shim] fbb_ipi_notify error: failed to open %%s (errno=%%d)\\n", path, errno);
    } else {
        char buf[32];
        ssize_t r = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (r > 0) {
            buf[r] = '\\0';
            pid_t pid = (pid_t)atoi(buf);
            if (pid > 0) {
                fprintf(stderr, "[Shim] fbb_ipi_notify(dest_cpu=%%d) sending signal to PID=%%d\\n", dest_cpu, pid);
                kill(pid, dest_cpu == 0 ? SIGUSR2 : SIGUSR1);
            } else {
                fprintf(stderr, "[Shim] fbb_ipi_notify error: invalid pid string '%%s'\\n", buf);
            }
        } else {
            fprintf(stderr, "[Shim] fbb_ipi_notify error: read returned %%zd (errno=%%d)\\n", r, errno);
        }
    }
}

#ifdef FBB_WITH_OPENAMP
#include <metal/sys.h>
#include <metal/device.h>
#include <metal/alloc.h>
#include <metal/io.h>
#include <openamp/open_amp.h>
#include <openamp/rpmsg_virtio.h>
#include <pthread.h>
#include <poll.h>

static struct rpmsg_virtio_device rvdev;
static struct rpmsg_endpoint ept;
static pthread_t rpmsg_thread;
static int rpmsg_running = 0;
static int app_socket_fd = -1;
static int shim_socket_fd = -1;
static int signal_pipe[2] = {-1, -1};
static void *rpmsg_shm_ptr = NULL;

static uint8_t shim_virtio_get_status(struct virtio_device *vdev) {
    (void)vdev;
    if (!rpmsg_shm_ptr) return 0;
    return *(volatile uint8_t *)((char *)rpmsg_shm_ptr + 0x7ff0);
}

static void shim_virtio_set_status(struct virtio_device *vdev, uint8_t status) {
    (void)vdev;
    if (rpmsg_shm_ptr) {
        *(volatile uint8_t *)((char *)rpmsg_shm_ptr + 0x7ff0) = status;
    }
}

static uint32_t shim_virtio_get_features(struct virtio_device *vdev) {
    (void)vdev;
    return 0;
}

static void shim_virtio_set_features(struct virtio_device *vdev, uint32_t features) {
    (void)vdev;
    (void)features;
}

static void shim_virtio_notify(struct virtqueue *vq) {
    (void)vq;
    fbb_ipi_notify(1); // Notify M-Core
}

static const struct virtio_dispatch shim_virtio_dispatch = {
    .get_status = shim_virtio_get_status,
    .set_status = shim_virtio_set_status,
    .get_features = shim_virtio_get_features,
    .set_features = shim_virtio_set_features,
    .notify = shim_virtio_notify,
};

static int shim_ept_cb(struct rpmsg_endpoint *ept, void *data, size_t len, uint32_t src, void *priv) {
    (void)ept;
    (void)priv;
    (void)src;
    fprintf(stderr, "[Shim] shim_ept_cb received reply of %%zu bytes from M-core, writing to app\\n", len);
    if (shim_socket_fd != -1) {
        if (!original_write) original_write = dlsym(RTLD_NEXT, "write");
        original_write(shim_socket_fd, data, len);
    }
    return RPMSG_SUCCESS;
}

static void *rpmsg_worker_thread(void *arg) {
    (void)arg;
    fprintf(stderr, "[Shim] rpmsg_worker_thread started\\n");
    struct pollfd fds[2];
    fds[0].fd = shim_socket_fd;
    fds[0].events = POLLIN;
    fds[1].fd = signal_pipe[0];
    fds[1].events = POLLIN;

    while (rpmsg_running) {
        int ret = poll(fds, 2, 100);
        if (ret > 0) {
            if (fds[1].revents & POLLIN) {
                char dummy;
                read(signal_pipe[0], &dummy, 1);
                fprintf(stderr, "[Shim] Worker received signal notification, notifying rproc\\n");
                rproc_virtio_notified(rvdev.vdev, 100);
            }
            if (fds[0].revents & POLLIN) {
                char buf[512];
                ssize_t len = read(shim_socket_fd, buf, sizeof(buf));
                if (len > 0) {
                    fprintf(stderr, "[Shim] Worker received %%zd bytes from app, sending to OpenAMP\\n", len);
                    rpmsg_send(&ept, buf, len);
                }
            }
        }
    }
    fprintf(stderr, "[Shim] rpmsg_worker_thread exiting\\n");
    return NULL;
}

static void shim_sigusr2_handler(int sig) {
    (void)sig;
    fprintf(stderr, "[Shim] shim_sigusr2_handler fired!\\n");
    if (signal_pipe[1] != -1) {
        char dummy = 1;
        if (!original_write) original_write = dlsym(RTLD_NEXT, "write");
        original_write(signal_pipe[1], &dummy, 1);
    }
}

static int handle_rpmsg_open(void) {
    fprintf(stderr, "[Shim] handle_rpmsg_open entering\\n");
    if (app_socket_fd != -1) {
        return app_socket_fd;
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        return -1;
    }
    app_socket_fd = sv[0];
    shim_socket_fd = sv[1];

    if (pipe(signal_pipe) < 0) {
        close(sv[0]); close(sv[1]);
        return -1;
    }

    int shm_fd = original_open(SHM_FILE, O_RDWR, 0);
    if (shm_fd < 0) {
        close(sv[0]); close(sv[1]);
        close(signal_pipe[0]); close(signal_pipe[1]);
        return -1;
    }
    rpmsg_shm_ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (rpmsg_shm_ptr == MAP_FAILED) {
        close(sv[0]); close(sv[1]);
        close(signal_pipe[0]); close(signal_pipe[1]);
        return -1;
    }

    struct metal_init_params init_params = METAL_INIT_DEFAULTS;
    if (metal_init(&init_params) != 0) {
        munmap(rpmsg_shm_ptr, SHM_SIZE);
        close(sv[0]); close(sv[1]);
        close(signal_pipe[0]); close(signal_pipe[1]);
        return -1;
    }

    struct metal_device *mdev = NULL;
    struct metal_io_region *io = NULL;
    static metal_phys_addr_t shm_phys[] = { 0x3ee00000 };
    static struct metal_device shm_dev = {
        .name = "shm",
        .num_regions = 1,
        .regions = {
            {
                .virt = NULL,
                .physmap = shm_phys,
                .size = SHM_SIZE,
                .page_shift = 18,
                .page_mask = -1,
                .mem_flags = 0,
            }
        }
    };
    shm_dev.regions[0].virt = rpmsg_shm_ptr;
    metal_register_generic_device(&shm_dev);
    metal_device_open("generic", "shm", &mdev);
    io = metal_device_io_region(mdev, 0);

    static struct virtio_device vdev;
    vdev.role = VIRTIO_DEV_DRIVER;
    vdev.vrings_num = 2;
    vdev.func = &shim_virtio_dispatch;

    static struct virtio_vring_info vrings[2];
    vrings[0].io = io;
    vrings[0].info.align = 4096;
    vrings[0].info.num_descs = 16;
    vrings[0].info.vaddr = (void*)((uintptr_t)rpmsg_shm_ptr + 0x0000);
    vrings[0].vq = virtqueue_allocate(16);
    vrings[0].notifyid = 100;
    vrings[1].io = io;
    vrings[1].info.align = 4096;
    vrings[1].info.num_descs = 16;
    vrings[1].info.vaddr = (void*)((uintptr_t)rpmsg_shm_ptr + 0x4000);
    vrings[1].vq = virtqueue_allocate(16);
    vrings[1].notifyid = 101;

    vdev.vrings_info = vrings;

    static struct rpmsg_virtio_shm_pool shpool;
    rpmsg_virtio_init_shm_pool(&shpool, (void*)((uintptr_t)rpmsg_shm_ptr + 0x8000), SHM_SIZE - 0x8000);
    rpmsg_init_vdev(&rvdev, &vdev, NULL, io, &shpool);
    struct rpmsg_device *rdev = rpmsg_virtio_get_rpmsg_device(&rvdev);

    rpmsg_create_ept(&ept, rdev, "rpmsg-openamp-demo-channel", 100, 101, shim_ept_cb, NULL);

    signal(SIGUSR2, shim_sigusr2_handler);

    /* Write proxy_pid only for the actual application opening RPMsg */
    ensure_dir();
    if (!original_open) original_open = dlsym(RTLD_NEXT, "open");
    int proxy_fd = original_open("/tmp/fbb/sys/class/remoteproc/remoteproc0/proxy_pid", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (proxy_fd >= 0) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%%d\\n", getpid());
        if (!original_write) original_write = dlsym(RTLD_NEXT, "write");
        original_write(proxy_fd, buf, len);
        close(proxy_fd);
    }

    rpmsg_running = 1;
    pthread_create(&rpmsg_thread, NULL, rpmsg_worker_thread, NULL);

    if (app_socket_fd < MAX_FDS) {
        virtual_fd_route_idx[app_socket_fd] = -300;
    }
    fprintf(stderr, "[Shim] handle_rpmsg_open initialized successfully\\n");
    return app_socket_fd;
}
#else
static int handle_rpmsg_open(void) {
    errno = ENODEV;
    return -1;
}
#endif

static void __attribute__((constructor)) init_mcore_mapping(void) {
    char *fbb_mcore = getenv("FBB_MCORE");
    if (fbb_mcore && strcmp(fbb_mcore, "1") == 0) {
        if (!original_open) original_open = dlsym(RTLD_NEXT, "open");
        if (!original_mmap) original_mmap = dlsym(RTLD_NEXT, "mmap");
        for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++) {
            int shm_fd = original_open(routes[i].shm_path, O_RDWR, 0666);
            if (shm_fd < 0) {
                fprintf(stderr, "[Shim M-Core] ERROR: Failed to open %%s (errno: %%d)\\n", routes[i].shm_path, errno);
                continue;
            }
            void *addr = (void *)(uintptr_t)routes[i].base_addr;
            off_t offset = (off_t)(routes[i].base_addr - routes[0].base_addr);
            
            // Try safest first: MAP_FIXED_NOREPLACE (prevents overriding existing stack/heap mappings)
            void *mapped = original_mmap(addr, routes[i].size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, shm_fd, offset);
            if (mapped == MAP_FAILED) {
                if (errno == EEXIST) {
                    fprintf(stderr, "[Shim M-Core] CRITICAL ERROR: Address collision at 0x%%lx (already mapped, EEXIST)\\n", routes[i].base_addr);
                } else {
                    fprintf(stderr, "[Shim M-Core] ERROR: mmap MAP_FIXED_NOREPLACE failed for address 0x%%lx (errno: %%d)\\n", routes[i].base_addr, errno);
                }
            } else if (mapped != addr) {
                // If MAP_FIXED_NOREPLACE is ignored by an older kernel, it maps to a different address.
                // We must unmap it and fall back to MAP_FIXED.
                munmap(mapped, routes[i].size);
                mapped = original_mmap(addr, routes[i].size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shm_fd, offset);
                if (mapped == MAP_FAILED) {
                    fprintf(stderr, "[Shim M-Core] ERROR: Fallback MAP_FIXED failed for address 0x%%lx (errno: %%d)\\n", routes[i].base_addr, errno);
                } else {
                    fprintf(stdout, "[Shim M-Core] Successfully mapped 0x%%lx -> %%p with fallback MAP_FIXED\\n", routes[i].base_addr, mapped);
                    fflush(stdout);
                }
            } else {
                fprintf(stdout, "[Shim M-Core] Successfully mapped 0x%%lx -> %%p with MAP_FIXED_NOREPLACE (size: %%ld, offset: %%ld)\\n",
                        routes[i].base_addr, mapped, (long)routes[i].size, (long)offset);
                fflush(stdout);
            }
            close(shm_fd);
        }
    }
}

static void __attribute__((destructor)) cleanup_shim(void) {
    char *fbb_mcore = getenv("FBB_MCORE");
    if (!fbb_mcore || strcmp(fbb_mcore, "1") != 0) {
        unlink("/tmp/fbb/sys/class/remoteproc/remoteproc0/proxy_pid");
#ifdef FBB_WITH_OPENAMP
        if (rpmsg_running) {
            rpmsg_running = 0;
            pthread_join(rpmsg_thread, NULL);
            rpmsg_destroy_ept(&ept);
            rpmsg_deinit_vdev(&rvdev);
            if (app_socket_fd != -1) close(app_socket_fd);
            if (shim_socket_fd != -1) close(shim_socket_fd);
            if (signal_pipe[0] != -1) close(signal_pipe[0]);
            if (signal_pipe[1] != -1) close(signal_pipe[1]);
            if (rpmsg_shm_ptr) munmap(rpmsg_shm_ptr, SHM_SIZE);
            metal_finish();
        }
#endif
    }
}
""" % (
            ", ".join(mmap_routes),
            " ".join(i2c_matches),
            " ".join(uart_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches),
            "\n".join(rpmsg_matches)
        )

class RTLGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        devs = model.get_uio_devices()
        if not devs: return """/* verilator lint_off UNUSED */
module vfpga_top (
    input wire clk, input wire rst_n, input wire [31:0] addr, 
    input wire [31:0] w_data, input wire w_en, output reg [31:0] r_data
);
    always @(*) r_data = 32'hdeadbeef;
endmodule"""
        # 全UIO/GPIOデバイスのレジスタを物理アドレス付きで集約
        all_regs = []
        for dev in devs:
            for r in dev.registers:
                phys_addr = dev.base_addr + int(r.offset, 0)
                all_regs.append((r.name, phys_addr))

        reg_ports = ",\n".join(['    output reg [31:0] %s' % name for name, _ in all_regs])
        reset_logic = "\n".join(['            %s <= 32\'h0;' % name for name, _ in all_regs])
        write_cases = "\n".join(["                32'h%08x: %s <= w_data;" % (addr, name) for name, addr in all_regs])
        read_cases = "\n".join(["            32'h%08x: r_data = %s;" % (addr, name) for name, addr in all_regs])
        return """/* Auto-generated RTL Skeleton */
module vfpga_top (
    input wire clk,
    input wire rst_n,
    input wire [31:0] addr,
    input wire [31:0] w_data,
    input wire w_en,
    output reg [31:0] r_data,
    /* verilator lint_off UNUSED */
    input wire [117:0] l_pins_i,
    /* verilator lint_on UNUSED */
    output wire [117:0] l_pins_o,
    output wire [117:0] l_pins_t%s
);
    // GPIO Pin Mapping Logic (Generic Placeholder)
    assign l_pins_o = 118'h0;
    assign l_pins_t = 118'hFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF; // All inputs by default

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            %s
        end else if (w_en) begin
            case (addr)
%s
                default: ;
            endcase
        end
    end
    always @(*) begin
        case (addr)
%s
            default: r_data = 32'hdeadbeef;
        endcase
    end
endmodule
""" % (("," + reg_ports) if reg_ports else "", reset_logic, write_cases, read_cases)

class SimulatorGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        devs = model.get_uio_devices()
        # 全UIO/GPIOデバイスのレジスタを物理アドレスで集約
        reg_defs = []
        for dev in devs:
            for r in dev.registers:
                phys_addr = dev.base_addr + int(r.offset, 0)
                reg_defs.append('    { .name="%s", .addr=0x%08x }' % (r.name, phys_addr))

        # SHMベースアドレス (最小のデバイスベースアドレス)
        min_base = min(d.base_addr for d in devs) if devs else 0

        return """
#include <stdio.h>
#include <iostream>
#include <verilated.h>
#include "Vvfpga_top.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <verilated_vcd_c.h>
#include "vfpga_config.h"
#include "sim_traits.h"

double sc_time_stamp() { return 0; }

VerilatedVcdC* volatile_trace = nullptr;

void handle_sigterm(int sig) {
    if (volatile_trace) {
        volatile_trace->close();
    }
    _exit(sig);
}

#define SHM_BASE_ADDR 0x%08xU

struct RegMeta { const char* name; uint32_t addr; };
static RegMeta registers[] = { %s };

template <typename T>
void run_sim_loop(T* top, volatile uint32_t* shm, uint32_t* old_shm, VerilatedVcdC* m_trace, uint64_t& vtime) {
    // Initial Reset Sequence
    if constexpr (has_rst_n<T>::value) top->rst_n = 1;
    if constexpr (has_clk<T>::value) top->clk = 0;
    top->eval(); m_trace->dump(vtime++);
    
    if constexpr (has_rst_n<T>::value) top->rst_n = 0;
    top->eval(); 
    if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
    m_trace->dump(vtime++);
    
    if constexpr (has_rst_n<T>::value) top->rst_n = 1;
    top->eval(); m_trace->dump(vtime++);

    printf("[Sim] Simulator Started (SHM: %%s)\\n", SHM_FILE); fflush(stdout);
        int loop_count = 0;
        while (!Verilated::gotFinish()) {
            if (loop_count++ %% 10000 == 0) {
                printf("[Sim Debug] ");
                for (int j = 0; j < 16; j++) {
                    printf("shm[%%d]=0x%%x ", j, shm[j]);
                }
                printf("\\n"); fflush(stdout);
            }
            // Synchronize Write from SHM to RTL
            for (int i = 0; i < %d; i++) {
                uint32_t off = (registers[i].addr - SHM_BASE_ADDR) / 4;
                if (shm[off] != old_shm[off]) {
                    if constexpr (has_addr<T>::value) top->addr = registers[i].addr;
                    if constexpr (has_w_data<T>::value) top->w_data = shm[off];
                    if constexpr (has_w_en<T>::value) top->w_en = 1;
                    
                    top->eval(); 
                    if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
                    
                    if constexpr (has_w_en<T>::value) top->w_en = 0;
                    old_shm[off] = shm[off];

                    // Virtual IPI Register (TRIG) detection, signal relay and auto-clear
                    if (strcmp(registers[i].name, "TRIG") == 0 && shm[off] == 0) {
                        FILE* pf = fopen("/tmp/fbb/sys/class/remoteproc/remoteproc0/proxy_pid", "r");
                        if (pf) {
                            int pid = 0;
                            if (fscanf(pf, "%%d", &pid) == 1 && pid > 0) {
                                kill(pid, SIGUSR2);
                            }
                            fclose(pf);
                        }
                        shm[off] = 0xFFFFFFFF;
                        old_shm[off] = 0xFFFFFFFF;
                        if constexpr (has_addr<T>::value) top->addr = registers[i].addr;
                        if constexpr (has_w_data<T>::value) top->w_data = 0xFFFFFFFF;
                        if constexpr (has_w_en<T>::value) top->w_en = 1;
                        top->eval();
                        if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
                        if constexpr (has_w_en<T>::value) top->w_en = 0;
                    }
                }
            }

            // UI Injection Handling for Input Pins
            // If TRI bit is 1, apply SHM value to l_pins_i
            // (Assuming last 4 words of SHM are injection area)
            for (int b = 0; b < 118; b++) {
                int word = b / 32;
                int bit = b %% 32;
                // Simplified: Mapping logic should match Dashboard's gpio-inject
                // top->l_pins_i[b] = (shm[SHM_SIZE/4 - 4 + word] >> bit) & 0x1;
            }

            // Synchronize Read from RTL to SHM
            for (int i = 0; i < %d; i++) {
                if constexpr (has_addr<T>::value) top->addr = registers[i].addr;
                top->eval();
                uint32_t off = (registers[i].addr - SHM_BASE_ADDR) / 4;
                
                if constexpr (has_r_data<T>::value) {
                    if (top->r_data != old_shm[off]) {
                        shm[off] = top->r_data; old_shm[off] = top->r_data;
                    }
                }
            }
            top->eval(); 
            if constexpr (has_clk<T>::value) { top->clk = 1; top->eval(); top->clk = 0; top->eval(); }
            m_trace->dump(vtime++);
            usleep(100);
        }
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vvfpga_top* top = new Vvfpga_top;
    int fd = open(SHM_FILE, O_CREAT | O_RDWR, 0666);
    if (ftruncate(fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        return 1;
    }
    volatile uint32_t* shm = (volatile uint32_t*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    uint32_t* old_shm = new uint32_t[SHM_SIZE/4]; memset(old_shm, 0, SHM_SIZE);

    Verilated::traceEverOn(true);
    VerilatedVcdC* m_trace = new VerilatedVcdC;
    volatile_trace = m_trace;
    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);
    top->trace(m_trace, 99);
    m_trace->open("vfpga.vcd");
    uint64_t vtime = 0;
    
    run_sim_loop(top, shm, old_shm, m_trace, vtime);

    m_trace->close();
    delete[] old_shm;
    return 0;
}
""" % (min_base, ", ".join(reg_defs), len(reg_defs), len(reg_defs))

class ManifestGenerator(BaseGenerator):
    def generate(self, model: BoardModel):
        import json
        shm_name = model.name
        shm_size = ConfigGenerator.compute_shm_size(model)
        project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../"))
        manifest = {
            "board": shm_name,
            "model": getattr(model, "model_name", "generic-vfpga"),
            "shm_path": f"/tmp/{shm_name}",
            "shm_size": shm_size,
            "project_root": project_root,
            "hdmi_output_path": "/tmp/hdmi_output.bmp",
            "devices": [],
            "uarts": [{"name": d.name, "port": int(d.extra_props.get("port", 2000))} for d in model.get_uart_devices()]
        }
        for dev in model.devices:
            dev_info = {
                "name": dev.name,
                "type": dev.type,
                "path": dev.path,
                "base_addr": dev.base_addr,
                "base_reg": dev.base_reg,
                "registers": [{"name": r.name, "logical_name": r.logical_name or r.name, "offset": r.offset} for r in dev.registers],
                "extra": dev.extra_props
            }
            manifest["devices"].append(dev_info)
        return json.dumps(manifest, indent=4)

class RustPACGenerator(BaseGenerator):
    def generate(self, model: BoardModel) -> str:
        devs = model.get_uio_devices()
        if not devs:
            return "// No UIO/GPIO devices defined in DTS for PAC generation.\n"
        
        struct_fields = []
        reg_instantiations = []
        
        for dev in devs:
            for r in dev.registers:
                phys_addr = dev.base_addr + int(r.offset, 0)
                field_name = r.name.lower()
                struct_fields.append(f"    pub {field_name}: Register<u32>,")
                reg_instantiations.append(f"            {field_name}: Register::new(0x{phys_addr:08x}),")
                
        struct_fields_str = "\n".join(struct_fields)
        reg_instantiations_str = "\n".join(reg_instantiations)
        
        return f"""// Auto-generated PAC (Peripheral Access Crate)
#![allow(unused)]

pub struct Register<T> {{
    ptr: *mut T,
}}

impl<T> Register<T> {{
    pub const fn new(address: usize) -> Self {{
        Self {{ ptr: address as *mut T }}
    }}
    
    pub fn read(&self) -> T where T: Copy {{
        unsafe {{ core::ptr::read_volatile(self.ptr) }}
    }}
    
    pub fn write(&self, value: T) where T: Copy {{
        unsafe {{ core::ptr::write_volatile(self.ptr, value) }}
    }}
}}

pub struct Vfpga {{
{struct_fields_str}
}}

impl Vfpga {{
    pub const fn new() -> Self {{
        Self {{
{reg_instantiations_str}
        }}
    }}
}}

pub struct Peripherals {{
    pub vfpga: Vfpga,
}}

static mut TAKEN: bool = false;

impl Peripherals {{
    pub fn take() -> Option<Self> {{
        unsafe {{
            if TAKEN {{
                None
            }} else {{
                TAKEN = true;
                Some(Self {{
                    vfpga: Vfpga::new(),
                }})
            }}
        }}
    }}
}}
"""

class GeneratorOrchestrator:
    def __init__(self, model: BoardModel, dts_path: str = None):
        self.model = model
        self.dts_path = dts_path
        # プロジェクトルートを取得
        self.project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../"))
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

if __name__ == "__main__":
    if len(sys.argv) < 2: sys.exit(1)
    model = DTSParser.parse(sys.argv[1])
    GeneratorOrchestrator(model, sys.argv[1]).generate_all()
