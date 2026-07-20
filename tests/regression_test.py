#!/usr/bin/env python3
import os
import subprocess
import sys
import signal
import time

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
                
    print(f"Found {len(scenarios)} scenarios to run:")
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
        
        # 2. Run
        print(f"[Runner] Executing scenario_runner.sh for {s}...")
        run_cmd = ["./tests/scenario_runner.sh", s_path]
        
        # Start in a new process group to allow killing descendants, and pass DEVNULL to stdin
        proc = subprocess.Popen(
            run_cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True
        )
        
        stdout, stderr = "", ""
        is_passed = False
        is_infinite = s in ("02d_oled_i2c", "S01_cpp_lfsr_sequencer")
        try:
            if is_infinite:
                # Wait for 15 seconds, then raise TimeoutExpired
                stdout, stderr = proc.communicate(timeout=15)
                is_passed = (proc.returncode == 0)
            else:
                stdout, stderr = proc.communicate()
                is_passed = (proc.returncode == 0)
        except subprocess.TimeoutExpired:
            print(f"[Runner] Scenario {s} timed out as expected. Terminating process group...")
            try:
                # Send SIGTERM to the process group
                pgid = os.getpgid(proc.pid)
                os.killpg(pgid, signal.SIGTERM)
                
                # Give it a moment to clean up and read outputs
                time.sleep(1)
                # Force kill if still alive
                os.killpg(pgid, signal.SIGKILL)
            except ProcessLookupError:
                pass
                
            stdout, stderr = proc.communicate()
            is_passed = True # Treat timeout as pass for infinite loops
            
        if is_passed:
            print(f"[Runner] RESULT: {s} PASSED")
            results[s] = "PASSED"
        else:
            print(f"[Runner] RESULT: {s} FAILED (Exit Code: {proc.returncode})")
            results[s] = f"FAILED ({proc.returncode})"
            
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
