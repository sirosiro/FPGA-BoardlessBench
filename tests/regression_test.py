#!/usr/bin/env python3
import os
import subprocess
import sys
import signal
import time

def probe_host_environment():
    """
    Measure host compilation and filesystem latency to automatically
    calibrate appropriate build and execution timeouts.
    """
    if "FBB_BUILD_TIMEOUT" in os.environ and "FBB_TEST_TIMEOUT" in os.environ:
        b_to = int(os.environ["FBB_BUILD_TIMEOUT"])
        a_to = int(os.environ["FBB_TEST_TIMEOUT"])
        return b_to, a_to, f"User configured (Build: {b_to}s, Test: {a_to}s)"

    probe_file = "fbb_probe_tmp.c"
    probe_obj = "fbb_probe_tmp.o"
    probe_start = time.time()
    try:
        with open(probe_file, "w") as f:
            f.write("int main(void) { return 0; }\n")
        subprocess.run(
            ["gcc", "-O2", "-c", probe_file, "-o", probe_obj],
            capture_output=True,
            timeout=10
        )
        probe_dt = time.time() - probe_start
    except Exception:
        probe_dt = 0.50
    finally:
        if os.path.exists(probe_file):
            os.remove(probe_file)
        if os.path.exists(probe_obj):
            os.remove(probe_obj)

    # Robust environment detection: check container runtime, kernel markers (WSL/linuxkit/microsoft), or probe latency
    is_virtualized = (probe_dt > 0.08 or os.path.exists("/.dockerenv") or os.path.exists("/run/.containerenv"))
    if not is_virtualized:
        try:
            with open("/proc/version", "r") as f:
                v = f.read().lower()
                if any(k in v for k in ["microsoft", "wsl", "linuxkit"]):
                    is_virtualized = True
        except Exception:
            pass

    if is_virtualized:
        env_desc = f"Container / Virtualized Environment detected (Probe: {probe_dt:.2f}s)"
        default_build_timeout = 360  # 6 minutes for full Verilator + C++ build + git clones
        default_app_timeout = 60     # 60 seconds for app execution
    else:
        env_desc = f"Native Linux / Fast I/O detected (Probe: {probe_dt:.2f}s)"
        default_build_timeout = 240  # 4 minutes for full build
        default_app_timeout = 45     # 45 seconds for app execution

    b_to = int(os.environ.get("FBB_BUILD_TIMEOUT", default_build_timeout))
    a_to = int(os.environ.get("FBB_TEST_TIMEOUT", default_app_timeout))
    return b_to, a_to, env_desc

def main():
    scenarios_dir = "tests/scenarios"
    if not os.path.exists(scenarios_dir):
        print(f"Error: {scenarios_dir} does not exist.")
        sys.exit(1)
        
    scenarios = []
    for item in sorted(os.listdir(scenarios_dir)):
        path = os.path.join(scenarios_dir, item)
        if os.path.isdir(path):
            run_sh = os.path.join(path, "run.sh")
            config_dts = os.path.join(path, "config.dts")
            if os.path.exists(run_sh) and os.path.exists(config_dts):
                scenarios.append(item)
                
    if len(sys.argv) > 1:
        targets = set(sys.argv[1:])
        scenarios = [s for s in scenarios if s in targets]

    build_timeout, app_timeout, env_desc = probe_host_environment()

    print("=" * 60)
    print(f"F-BB Regression Test Harness (Auto-Calibrating)")
    print(f"Host Environment : {env_desc}")
    print(f"Timeouts         : Build Phase = {build_timeout}s | Execution Phase = {app_timeout}s")
    print(f"Total Scenarios  : {len(scenarios)}")
    print("=" * 60)
    for s in scenarios:
        print(f"  - {s}")
    print("-" * 60)
    
    results = {}
    
    for s in scenarios:
        s_path = os.path.join(scenarios_dir, s)
        print(f"\n[Runner] >>> Starting Test: {s} <<<")
        
        # 1. Clean
        print(f"[Runner] Cleaning environment for {s}...")
        clean_cmd = ["./tests/scenario_runner.sh", s_path, "--clean"]
        subprocess.run(clean_cmd, capture_output=True, text=True)
        if os.path.exists("/tmp/fbb_memory_violation"):
            os.remove("/tmp/fbb_memory_violation")
        
        # Ensure non-interactive batch test environment by completely unsetting VFPGA_INTERACTIVE
        test_env = os.environ.copy()
        test_env.pop("VFPGA_INTERACTIVE", None)

        # 2. Phase 1: Build Phase (Generous timeout, fails fast on compiler errors)
        print(f"[Runner] [Phase 1/2] Building simulation engine & application (timeout: {build_timeout}s)...")
        build_cmd = ["./tests/scenario_runner.sh", s_path, "--build-only"]
        build_proc = subprocess.Popen(
            build_cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
            env=test_env
        )
        b_stdout, b_stderr = "", ""
        try:
            b_stdout, b_stderr = build_proc.communicate(timeout=build_timeout)
            build_passed = (build_proc.returncode == 0)
        except subprocess.TimeoutExpired:
            print(f"[Runner] Scenario {s} build timed out (exceeded {build_timeout}s limit). Terminating...")
            try:
                pgid = os.getpgid(build_proc.pid)
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            b_stdout, b_stderr = build_proc.communicate()
            results[s] = "FAILED (Build Timeout)"
            print(f"[Runner] RESULT: {s} FAILED (Build Timeout)")
            continue

        if not build_passed:
            results[s] = f"FAILED (Build Error: {build_proc.returncode})"
            print(f"[Runner] RESULT: {s} FAILED (Build Error: {build_proc.returncode})")
            print("--- Build Output (last 30 lines) ---")
            lines = b_stdout.splitlines() + b_stderr.splitlines()
            for line in lines[-30:]:
                print(line)
            print("-" * 60)
            continue

        # 3. Phase 2: App Execution Phase (Monitored for hangs, deadlocks, and assertions)
        is_infinite = s in ("S01_cpp_lfsr_sequencer",)
        scenario_app_timeout = 15 if is_infinite else app_timeout
        print(f"[Runner] [Phase 2/2] Executing scenario with RTL simulator (timeout: {scenario_app_timeout}s)...")
        run_cmd = ["./tests/scenario_runner.sh", s_path, "--skip-build"]
        
        proc = subprocess.Popen(
            run_cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
            env=test_env
        )
        
        stdout, stderr = "", ""
        is_passed = False
        try:
            stdout, stderr = proc.communicate(timeout=scenario_app_timeout)
            is_passed = (proc.returncode == 0)
        except subprocess.TimeoutExpired:
            if is_infinite:
                print(f"[Runner] Scenario {s} timed out as expected. Terminating process group...")
            else:
                print(f"[Runner] Scenario {s} execution timed out (exceeded {scenario_app_timeout}s limit)...")
            try:
                pgid = os.getpgid(proc.pid)
                os.killpg(pgid, signal.SIGTERM)
                time.sleep(1)
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                pass
                
            try:
                stdout, stderr = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                stdout, stderr = proc.communicate()
            is_passed = is_infinite  # Only treat timeout as pass for intentional infinite loops
            
        if is_passed:
            print(f"[Runner] RESULT: {s} PASSED")
            results[s] = "PASSED"
        else:
            exit_code = proc.returncode
            fail_reason = "Execution Timeout" if exit_code == -15 else f"Exit Code: {exit_code}"
            print(f"[Runner] RESULT: {s} FAILED ({fail_reason})")
            results[s] = f"FAILED ({fail_reason})"
            
            # Print last 30 lines of runner output
            print("--- Runner Output (last 30 lines) ---")
            lines = stdout.splitlines() + stderr.splitlines()
            for line in lines[-30:]:
                print(line)
                
            # Print controller.log and simulator.log if they exist
            controller_log = os.path.join(s_path, "controller.log")
            if os.path.exists(controller_log):
                print(f"--- controller.log (last 20 lines) ---")
                with open(controller_log, "r") as f:
                    c_lines = f.readlines()
                    for line in c_lines[-20:]:
                        print(line.strip())
                        
            simulator_log = os.path.join(s_path, "simulator.log")
            if os.path.exists(simulator_log):
                print(f"--- simulator.log (last 20 lines) ---")
                with open(simulator_log, "r") as f:
                    s_lines = f.readlines()
                    for line in s_lines[-20:]:
                        print(line.strip())
            
            print("-" * 60)
            
    print("\n" + "=" * 60)
    print("TEST EXECUTION SUMMARY")
    print("=" * 60)
    passed_count = 0
    failed_count = 0
    for s, res in results.items():
        print(f"  {s:<40}: {res}")
        if "PASSED" in res:
            passed_count += 1
        else:
            failed_count += 1
            
    print("-" * 60)
    print(f"Total: {len(results)} | Passed: {passed_count} | Failed: {failed_count}")
    print("=" * 60)
    
    if failed_count > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
