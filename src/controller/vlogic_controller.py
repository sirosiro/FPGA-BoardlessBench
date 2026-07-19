import time
import sys
import re
import os
import mmap
import glob
import socket
import threading
import select
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
SHM_NAME = "vfpga_reg"
excluded_uarts = set()

sys.path.append(os.path.join(PROJECT_ROOT, "scripts"))
from vfpga.parser import DTSParser

LAUNCHER_REGISTRY = {
    'i2c': {
        'atmel,24c02': {
            'binary': 'fbb_i2c_eeprom',
            'args': lambda p: ["--socket", f"/tmp/fbb_i2c_b{p['bus_id']}_a{p['addr']:02x}", "--init-val", str(p['init_val'])] + (["--file", p['mock_file'] if os.path.isabs(p['mock_file']) else os.path.join(PROJECT_ROOT, p['mock_file'])] if p.get('mock_file') else [])
        },
        'solomon,ssd1306': {
            'binary': 'fbb_i2c_oled',
            'args': lambda p: ["--socket", f"/tmp/fbb_i2c_b{p['bus_id']}_a{p['addr']:02x}"],
            'log': 'i2c_oled_daemon.log'
        }
    },
    'uart': {
        'uart_loopback': {
            'binary': 'fbb_uart_loopback',
            'args': lambda p: ["--pts-file", os.path.join(PROJECT_ROOT, f"dashboard/data/vfpga_uart_{p['uart_id']}")],
            'log': 'loopback_daemon.log'
        }
    },
    'spi': {
        'winbond,w25q128': {
            'binary': 'fbb_spi_flash',
            'args': lambda p: ["--socket", f"/tmp/fbb_spi_b{p['bus_id']}_c{p['cs']}"] + (["--file", p['mock_file'] if os.path.isabs(p['mock_file']) else os.path.join(PROJECT_ROOT, p['mock_file'])] if p.get('mock_file') else [])
        },
        'microchip,mcp3208': {
            'binary': 'fbb_spi_adc',
            'args': lambda p: ["--socket", f"/tmp/fbb_spi_b{p['bus_id']}_c{p['cs']}", "--init-val", str(p['init_val'])]
        }
    }
}


def get_peripherals_from_dts(dts_path):
    peripherals = []
    try:
        if not os.path.exists(dts_path):
            return peripherals
        board_model = DTSParser.parse(dts_path)
        uart_count = 0
        for dev in board_model.devices:
            if dev.type == 'i2c':
                bus_id = int(dev.extra_props.get('bus_id', '1'), 0)
                for slave in dev.i2c_slaves:
                    peripherals.append({
                        'type': 'i2c',
                        'bus_id': bus_id,
                        'addr': slave.addr,
                        'compatible': slave.compatible,
                        'mock_file': slave.mock_file,
                        'init_val': slave.init_val
                    })
            elif dev.type == 'spi':
                bus_id = int(dev.extra_props.get('bus_id', '0'), 0)
                for slave in dev.spi_slaves:
                    peripherals.append({
                        'type': 'spi',
                        'bus_id': bus_id,
                        'cs': slave.cs,
                        'compatible': slave.compatible,
                        'mock_file': slave.mock_file,
                        'init_val': slave.init_val
                    })
            elif dev.type == 'uart':
                uart_count += 1
                peripheral_type = dev.extra_props.get('fbb,peripheral-type')
                if peripheral_type:
                    peripherals.append({
                        'type': 'uart',
                        'compatible': dev.extra_props.get('compatible', 'xlnx,xps-uart'),
                        'peripheral_type': peripheral_type,
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
        board_model = DTSParser.parse(dts_path)
        for dev in board_model.devices:
            is_uio = (dev.type == 'uio')
            is_gpio = (dev.type == 'gpio')
            regions.append({
                'name': dev.name,
                'base_addr': dev.base_addr,
                'size': dev.size,
                'is_uio': is_uio,
                'is_gpio': is_gpio
            })
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
    base_port_env = os.getenv("FBB_UART_BASE_PORT")
    if base_port_env:
        try:
            base_port = int(base_port_env)
        except ValueError:
            base_port = 2000
    else:
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

def remoteproc_monitor_thread(dts_path):
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
                    if current_proc is not None:
                        status = current_proc.poll()
                        if status is None:
                            process_running = True
                        else:
                            current_proc.wait()
                            current_proc = None
                    
                    if not process_running and existing_pid > 0:
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
                        if "FBB_SD_DIR" not in env:
                            scenario_dir = os.path.dirname(os.path.abspath(dts_path))
                            env["FBB_SD_DIR"] = os.path.join(scenario_dir, "sd_card")
                        
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
    scenario_dir = os.path.dirname(os.path.abspath(dts_path))
    try:
        os.makedirs("/tmp/fbb", exist_ok=True)
        with open("/tmp/fbb_active_scenario", "w") as f_act:
            f_act.write(scenario_dir + "\n")
    except Exception as e:
        print(f"[Python] Warning: Could not write active scenario file: {e}")
    
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
    t_rproc = threading.Thread(target=remoteproc_monitor_thread, args=(dts_path,), daemon=True)
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

    # Start virtual peripherals from DTS (already populated in main start)
    peripheral_processes = []
    for p in peripherals:
        p_type = p.get('type')
        compat = p.get('compatible') if p_type != 'uart' else p.get('peripheral_type')
        
        registry = LAUNCHER_REGISTRY.get(p_type, {}).get(compat)
        if not registry:
            print(f"[Python] Warning: No launcher registered for peripheral {p_type}/{compat}")
            continue
            
        bin_name = registry['binary']
        bin_path = os.path.join(PROJECT_ROOT, f"build/bin/{bin_name}")
        if not os.path.exists(bin_path):
            tmp_bin = f"/tmp/fbb_build/bin/{bin_name}"
            if os.path.exists(tmp_bin):
                bin_path = tmp_bin
                
        args = [bin_path] + registry['args'](p)
        print(f"[Python] Starting Virtual {p_type.upper()} ({compat}): {' '.join(args)}")
        
        try:
            if 'log' in registry:
                log_path = os.path.join(os.path.dirname(dts_path), registry['log'])
                log_file = open(log_path, "w")
                proc = subprocess.Popen(args, stdout=log_file, stderr=subprocess.STDOUT)
            else:
                proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            peripheral_processes.append(proc)
        except Exception as e:
            print(f"[Python] Error starting daemon for {p_type}/{compat}: {e}")

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
