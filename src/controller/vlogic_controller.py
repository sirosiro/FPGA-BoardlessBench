import time
import sys
import re
import os
import mmap
import glob
import socket
import threading
import select

# プロジェクトルートの動的取得 (src/controller/vlogic_controller.py から見て 2つ上の階層)
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
SHM_NAME = "vfpga_reg"
excluded_uarts = set()

def get_peripherals_from_dts(dts_path):
    peripherals = []
    try:
        if not os.path.exists(dts_path):
            return peripherals
        with open(dts_path, 'r') as f:
            content = f.read()
        
        # Helper to matching braces
        def find_braces(text, start):
            b_start = text.find('{', start)
            if b_start == -1: return -1, -1
            count = 1
            i = b_start + 1
            while i < len(text) and count > 0:
                if text[i] == '{': count += 1
                elif text[i] == '}': count -= 1
                i += 1
            return b_start, i
            
        # 1. Look for root node '/'
        root_match = re.search(r'/\s*\{', content)
        if root_match:
            brace_start, brace_end = find_braces(content, root_match.start())
            if brace_start != -1:
                content = content[brace_start + 1 : brace_end - 1]

        pos = 0
        uart_count = 0
        while True:
            match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', content[pos:])
            if not match: break
            match_start = pos + match.start()
            raw_name = match.group(1).strip()
            b_start, b_end = find_braces(content, match_start)
            if b_start == -1:
                pos = match_start + len(raw_name) + 1
                continue
            
            body = content[b_start + 1 : b_end - 1]
            pos = b_end
            
            node_name = raw_name
            if ':' in raw_name:
                node_name = raw_name.split(':')[-1].strip()

            # Clean body by removing nested sub-node blocks to prevent property overriding
            clean_body = body
            while True:
                sub_match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', clean_body)
                if not sub_match:
                    break
                sub_start = sub_match.start()
                sub_brace_start, sub_brace_end = find_braces(clean_body, sub_start)
                if sub_brace_start == -1:
                    break
                clean_body = clean_body[:sub_start] + clean_body[sub_brace_end:]

            comp_match = re.search(r'compatible\s*=\s*"([^"]+)"', clean_body)
            compat = comp_match.group(1) if comp_match else ""
            
            if 'i2c' in compat or 'cdns,i2c' in compat:
                bus_match = re.search(r'bus_id\s*=\s*<([^>]+)>', clean_body)
                bus_id = int(bus_match.group(1).strip(), 0) if bus_match else 1
                
                sub_pos = 0
                while True:
                    sub_match = re.search(r'([a-zA-Z0-9_@:-]+)\s*\{', body[sub_pos:])
                    if not sub_match: break
                    sub_match_start = sub_pos + sub_match.start()
                    sub_raw_name = sub_match.group(1).strip()
                    sb_start, sb_end = find_braces(body, sub_match_start)
                    if sb_start == -1:
                        sub_pos = sub_match_start + len(sub_raw_name) + 1
                        continue
                    
                    sub_body = body[sb_start + 1 : sb_end - 1]
                    sub_pos = sb_end
                    
                    s_node_name = sub_raw_name
                    if ':' in sub_raw_name:
                        s_node_name = sub_raw_name.split(':')[-1].strip()

                    s_name = s_node_name.split('@')[0]
                    s_addr_str = s_node_name.split('@')[1] if '@' in s_node_name else "0"
                    try:
                        s_addr = int(s_addr_str, 16)
                    except:
                        s_addr = 0
                    
                    s_comp_match = re.search(r'compatible\s*=\s*"([^"]+)"', sub_body)
                    s_compat = s_comp_match.group(1) if s_comp_match else ""
                    
                    mock_file_match = re.search(r'fbb,mock-file\s*=\s*"([^"]+)"', sub_body)
                    mock_file = mock_file_match.group(1) if mock_file_match else None
                    
                    init_data_match = re.search(r'fbb,mock-data\s*=\s*<([^>]+)>', sub_body)
                    try:
                        init_val = int(init_data_match.group(1).strip(), 0) if init_data_match else 0x10
                    except:
                        init_val = 0x10
                    
                    if s_compat:
                        peripherals.append({
                            'type': 'i2c',
                            'bus_id': bus_id,
                            'addr': s_addr,
                            'compatible': s_compat,
                            'mock_file': mock_file,
                            'init_val': init_val
                        })
            
            elif 'uart' in compat or 'xlnx,xps-uart' in compat:
                uart_count += 1
                p_type_match = re.search(r'fbb,peripheral-type\s*=\s*"([^"]+)"', body)
                if p_type_match:
                    peripherals.append({
                        'type': 'uart',
                        'compatible': compat,
                        'peripheral_type': p_type_match.group(1),
                        'uart_id': uart_count
                    })
    except Exception as e:
        print(f"[Python] Error parsing peripherals in DTS: {e}")
    return peripherals

def get_shm_info_from_dts(dts_path):
    regions = []
    try:
        if not os.path.exists(dts_path):
            return regions
        with open(dts_path, 'r') as f:
            content = f.read()
        
        matches = re.finditer(r'([a-zA-Z0-9_@]+)\s*\{([^}]+)\}', content)
        for match in matches:
            raw_name = match.group(1).strip()
            name = raw_name.split('@')[0]
            body = match.group(2)
            
            comp_match = re.search(r'compatible\s*=\s*"([^"]+)"', body)
            label_match = re.search(r'label\s*=\s*"([^"]+)"', body)
            is_uio = False
            is_gpio = False
            if comp_match:
                compat = comp_match.group(1)
                if 'generic-uio' in compat:
                    is_uio = True
                elif 'gpio' in compat or 'xlnx,xps-gpio' in compat:
                    is_gpio = True
            # label が /dev/uio で始まるデバイスも UIO として扱う (カスタムIP対応)
            if not is_uio and not is_gpio and label_match and label_match.group(1).startswith('/dev/uio'):
                is_uio = True
            
            reg_match = re.search(r'reg\s*=\s*<([^>]+)>', body)
            if reg_match:
                try:
                    parts = reg_match.group(1).strip().split()
                    base_addr = int(parts[0], 0)
                    size = int(parts[1], 0) if len(parts) >= 2 else 0
                    regions.append({'name': name, 'base_addr': base_addr, 'size': size, 'is_uio': is_uio, 'is_gpio': is_gpio})
                except:
                    continue
    except Exception as e:
        print(f"[Python] Error parsing DTS: {e}")
    return regions


def uart_bridge_thread(pts_path, port):
    print(f"[Python] Starting UART bridge for {pts_path} on port {port}...")
    
    pts_fd = -1
    # 起動直後の極小のレースコンディションを回避するため、数回リトライする
    for i in range(10):
        try:
            pts_fd = os.open(pts_path, os.O_RDWR | os.O_NOCTTY)
            break
        except Exception as e:
            if i == 9:
                print(f"[Python] UART Bridge Final Error: {e}")
                return
            time.sleep(0.2)

    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind(('0.0.0.0', port))
    server_sock.listen(1)
    
    while True:
        try:
            conn, addr = server_sock.accept()
            print(f"[Python] UART {port} connected from {addr}")
            while True:
                r, w, e = select.select([pts_fd, conn], [], [])
                if pts_fd in r:
                    data = os.read(pts_fd, 1024)
                    if not data: break
                    conn.sendall(data)
                if conn in r:
                    data = conn.recv(1024)
                    if not data: break
                    os.write(pts_fd, data)
            conn.close()
        except Exception:
            break

def update_uart_map(active_bridges):
    import json
    try:
        mapping = {os.path.basename(f): port for f, port in active_bridges.items()}
        map_path = os.path.join(PROJECT_ROOT, "dashboard/data/uart_map.json")
        with open(map_path, "w") as f:
            json.dump(mapping, f)
    except Exception as e:
        print(f"[Python] Error updating UART map: {e}")

def uart_discovery_thread():
    # key: uart_file_path -> (pts_path, thread, port)
    active_bridges = {}
    base_port = 2000
    next_port = base_port
    print("[Python] UART Discovery thread started.")
    while True:
        glob_pattern = os.path.join(PROJECT_ROOT, "dashboard/data/vfpga_uart_*")
        files = glob.glob(glob_pattern)
        changed = False
        for f in files:
            # Skip if the UART is registered as an external emulation target
            try:
                uart_id = int(f.split('_')[-1])
                if uart_id in excluded_uarts:
                    continue
            except Exception:
                pass

            try:
                with open(f, 'r') as f_ptr:
                    pts_path = f_ptr.read().strip()
            except Exception:
                continue

            existing = active_bridges.get(f)
            # PTSパスが変わった、またはスレッドが終了していたら再起動
            if existing is None or existing[0] != pts_path or not existing[1].is_alive():
                port = existing[2] if existing else next_port
                if existing is None:
                    next_port += 1
                t = threading.Thread(target=uart_bridge_thread, args=(pts_path, port), daemon=True)
                t.start()
                active_bridges[f] = (pts_path, t, port)
                print(f"[Python] UART Found: {f} -> {pts_path} (TCP Port {port})")
                changed = True

        if changed:
            update_uart_map({f: v[2] for f, v in active_bridges.items()})
        time.sleep(1)

import subprocess
import signal

def remoteproc_monitor_thread():
    print("[Python] remoteproc Monitor thread started.")
    state_file = "/tmp/fbb/sys/class/remoteproc/remoteproc0/state"
    fw_file = "/tmp/fbb/sys/class/remoteproc/remoteproc0/firmware"
    pid_file = "/tmp/fbb/sys/class/remoteproc/remoteproc0/pid"
    
    current_proc = None
    
    while True:
        try:
            if os.path.exists(state_file):
                with open(state_file, "r") as f:
                    state = f.read().strip()
                
                if state == "start":
                    fw_name = ""
                    if os.path.exists(fw_file):
                        with open(fw_file, "r") as f:
                            fw_name = f.read().strip()
                    
                    if not fw_name or fw_name == "none":
                        print("[Python] remoteproc ERROR: No firmware specified!")
                        with open(state_file, "w") as f:
                            f.write("offline\n")
                        time.sleep(0.1)
                        continue
                    
                    mcore_path = ""
                    fw_try1 = f"/tmp/fbb/lib/firmware/{fw_name}"
                    fw_try2 = f"/lib/firmware/{fw_name}"
                    
                    if os.path.exists(fw_try1):
                        mcore_path = fw_try1
                    elif os.path.exists(fw_try2):
                        mcore_path = fw_try2
                    elif fw_name.startswith("/") or fw_name.startswith("."):
                        mcore_path = fw_name
                    else:
                        mcore_path = fw_name

                    existing_pid = 0
                    if os.path.exists(pid_file):
                        with open(pid_file, "r") as f:
                            try:
                                existing_pid = int(f.read().strip())
                            except:
                                pass
                    
                    process_running = False
                    if existing_pid > 0:
                        try:
                            os.kill(existing_pid, 0)
                            process_running = True
                        except OSError:
                            pass
                    
                    if not process_running:
                        print(f"[Python] remoteproc: Starting M-Core with FW: {mcore_path}")
                        env = os.environ.copy()
                        env["FBB_MCORE"] = "1"
                        env["LD_PRELOAD"] = os.path.join(PROJECT_ROOT, "libfpgashim.so")
                        
                        try:
                            current_proc = subprocess.Popen(
                                [mcore_path], 
                                env=env, 
                                stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                bufsize=1,
                                universal_newlines=True
                            )
                            
                            def forward_output(proc):
                                tty_out = None
                                try:
                                    tty_out = open('/dev/tty', 'w')
                                except Exception:
                                    pass
                                try:
                                    for line in proc.stdout:
                                        sys.stdout.write(line)
                                        sys.stdout.flush()
                                        if tty_out:
                                            try:
                                                tty_out.write(line)
                                                tty_out.flush()
                                            except Exception:
                                                pass
                                except Exception:
                                    pass
                                finally:
                                    if tty_out:
                                        try:
                                            tty_out.close()
                                        except Exception:
                                            pass
                                            
                            t_forward = threading.Thread(target=forward_output, args=(current_proc,), daemon=True)
                            t_forward.start()

                            with open(pid_file, "w") as f:
                                f.write(f"{current_proc.pid}\n")
                            with open(state_file, "w") as f:
                                f.write("running\n")
                            print(f"[Python] remoteproc: M-Core started successfully (PID: {current_proc.pid})")
                        except Exception as e:
                            print(f"[Python] remoteproc ERROR: Failed to execute {mcore_path}: {e}")
                            with open(state_file, "w") as f:
                                f.write("offline\n")
                    else:
                        with open(state_file, "w") as f:
                            f.write("running\n")
                            
                elif state == "stop":
                    existing_pid = 0
                    if os.path.exists(pid_file):
                        with open(pid_file, "r") as f:
                            try:
                                existing_pid = int(f.read().strip())
                            except:
                                pass
                    
                    if existing_pid > 0:
                        print(f"[Python] remoteproc: Stopping M-Core (PID: {existing_pid})...")
                        try:
                            os.kill(existing_pid, signal.SIGTERM)
                            for _ in range(20):
                                try:
                                    os.kill(existing_pid, 0)
                                    time.sleep(0.1)
                                except OSError:
                                    break
                            else:
                                os.kill(existing_pid, signal.SIGKILL)
                        except OSError:
                            pass
                    
                    with open(state_file, "w") as f:
                        f.write("offline\n")
                    with open(pid_file, "w") as f:
                        f.write("\n")
                    current_proc = None
                    print("[Python] remoteproc: M-Core stopped.")
            
        except Exception as e:
            print(f"[Python] remoteproc monitor error: {e}")
            
        time.sleep(0.1)

def main():
    if len(sys.argv) < 2:
        print("Usage: vlogic_controller.py <dts_path>")
        sys.exit(1)
        
    dts_path = sys.argv[1]
    
    # Clean up old virtual UART mapping files to prevent loopback daemons from reading stale PTY names
    glob_pattern = os.path.join(PROJECT_ROOT, "dashboard/data/vfpga_uart_*")
    for old_f in glob.glob(glob_pattern):
        try:
            os.remove(old_f)
        except OSError:
            pass
    
    # Load peripherals early to collect excluded PTY UARTs
    peripherals = get_peripherals_from_dts(dts_path)
    for p in peripherals:
        if p.get('type') == 'uart' and p.get('peripheral_type'):
            excluded_uarts.add(p.get('uart_id'))

    # Start discovery
    t = threading.Thread(target=uart_discovery_thread, daemon=True)
    t.start()

    # Start remoteproc monitor
    t_rproc = threading.Thread(target=remoteproc_monitor_thread, daemon=True)
    t_rproc.start()

    dts_path = sys.argv[1]
    regions = get_shm_info_from_dts(dts_path)
    
    # ジェネレータ側のロジックと合わせる (gen_vfpga.py と同じ計算)
    uio_gpio_devs = [r for r in regions if r.get('is_uio') or r.get('is_gpio')]
    
    # ボード名: UIO > GPIO > デフォルト
    uio = next((r for r in regions if r.get('is_uio')), None)
    if uio:
        board_name = uio['name']
    else:
        gpio = next((r for r in regions if r.get('is_gpio')), None)
        board_name = gpio['name'] if gpio else "vfpga_reg"
    
    # SHMサイズ: 全UIO/GPIOデバイスの物理アドレス範囲をカバー
    if len(uio_gpio_devs) == 0:
        board_size = 1024
    elif len(uio_gpio_devs) == 1:
        board_size = uio_gpio_devs[0]['size']
    else:
        min_addr = min(d['base_addr'] for d in uio_gpio_devs)
        max_end = max(d['base_addr'] + d['size'] for d in uio_gpio_devs)
        board_size = max_end - min_addr

    print(f"[Python] Starting Generic Virtual Logic Controller...")
    print(f"[Python] Using DTS: {dts_path}")
    
    # Start virtual peripherals from DTS (already populated in main start)
    peripheral_processes = []
    for p in peripherals:
        if p['type'] == 'i2c':
            if p['compatible'] == 'atmel,24c02':
                eeprom_bin = os.path.join(PROJECT_ROOT, "build/bin/fbb_i2c_eeprom")
                if not os.path.exists(eeprom_bin):
                    tmp_bin = "/tmp/fbb_build/bin/fbb_i2c_eeprom"
                    if os.path.exists(tmp_bin):
                        eeprom_bin = tmp_bin
                sock_path = f"/tmp/fbb_i2c_b{p['bus_id']}_a{p['addr']:02x}"
                args = [eeprom_bin, "--socket", sock_path, "--init-val", str(p['init_val'])]
                if p['mock_file']:
                    m_file = p['mock_file']
                    if not os.path.isabs(m_file):
                        m_file = os.path.join(PROJECT_ROOT, m_file)
                    args.extend(["--file", m_file])
                print(f"[Python] Starting Virtual I2C EEPROM: {' '.join(args)}")
                try:
                    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    peripheral_processes.append(proc)
                except Exception as e:
                    print(f"[Python] Error starting EEPROM daemon: {e}")
                    
        elif p['type'] == 'uart':
            if p['peripheral_type'] == 'uart_loopback':
                loopback_bin = os.path.join(PROJECT_ROOT, "build/bin/fbb_uart_loopback")
                if not os.path.exists(loopback_bin):
                    tmp_bin = "/tmp/fbb_build/bin/fbb_uart_loopback"
                    if os.path.exists(tmp_bin):
                        loopback_bin = tmp_bin
                pts_file = os.path.join(PROJECT_ROOT, f"dashboard/data/vfpga_uart_{p['uart_id']}")
                args = [loopback_bin, "--pts-file", pts_file]
                print(f"[Python] Starting Virtual UART Loopback: {' '.join(args)}")
                try:
                    daemon_log_path = os.path.join(os.path.dirname(dts_path), "loopback_daemon.log")
                    log_file = open(daemon_log_path, "w")
                    proc = subprocess.Popen(args, stdout=log_file, stderr=subprocess.STDOUT)
                    peripheral_processes.append(proc)
                except Exception as e:
                    print(f"[Python] Error starting UART Loopback daemon: {e}")

    path = f"/tmp/{board_name}"
    print(f"[Python] Creating SHM file: {path}, Size: {board_size}")
    
    shm = None
    try:
        # Create or open the file
        fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o666)
        os.ftruncate(fd, board_size)
        
        # Mmap it
        shm = mmap.mmap(fd, board_size)
        os.close(fd) # The mapping survives the close of the FD

        print("[Python] Backend is ready. Logic is handled by Verilator/RTL.")
        print("[Python] Press Ctrl+C to stop.")
        
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\n[Python] Stopping Controller...")
    finally:
        print("[Python] Cleaning up virtual peripheral processes...")
        for proc in peripheral_processes:
            try:
                proc.terminate()
                proc.wait(timeout=1)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass
        if shm is not None:
            shm.close()
        # The files in /tmp can stay or be cleaned up by start_lab.sh

if __name__ == "__main__":
    main()
