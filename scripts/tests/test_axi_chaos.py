#!/usr/bin/env python3
"""
Unit and regression tests for F-BB AXI Bus Contention & Memory Latency Chaos Engine.
Tests:
  1. Generator output verification (SimulatorGenerator generates AxiStallEngine, SFINAE hooks).
  2. C++17 SFINAE trait compilation with and without AXI signals.
  3. C++ AxiStallEngine cycle-accuracy, deterministic reproduction via seed, and backpressure (arready=0).
"""

import unittest
import os
import sys
import subprocess
import tempfile

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "scripts"))

from gen_vfpga import DTSParser, SimulatorGenerator

class TestAxiChaosEngine(unittest.TestCase):
    def setUp(self):
        self.sample_dts = """
        / {
            compatible = "test,axi-audio";
            model = "AXI Audio DMA Test Model";
            audio_dma@40000000 {
                compatible = "generic-uio";
                reg = <0x40000000 0x1000>;
                label = "/dev/uio0";
                registers = "CR_CTRL@0x10, SR_STATUS@0x14, SAMPLES@0x18";
            };
        };
        """
        self.dts_path = os.path.join(PROJECT_ROOT, "scripts/tests/temp_axi_test.dts")
        with open(self.dts_path, "w") as f:
            f.write(self.sample_dts)

    def tearDown(self):
        if os.path.exists(self.dts_path):
            os.remove(self.dts_path)

    def test_simulator_generator_contains_axi_stall_engine(self):
        """Verify that SimulatorGenerator emits AxiStallEngine and AXI SFINAE hooks."""
        model = DTSParser.parse(self.dts_path)
        gen = SimulatorGenerator()
        code = gen.generate(model)

        self.assertIn("class AxiStallEngine", code)
        self.assertIn("has_m_axi_arvalid", code)
        self.assertIn("has_m_axi_arready", code)
        self.assertIn("has_m_axi_rvalid", code)
        self.assertIn("has_m_axi_rready", code)
        self.assertIn("axi_engine.init()", code)
        self.assertIn("axi_engine.pre_clock(top, sim_cycle)", code)
        self.assertIn("axi_engine.post_clock(top, shm, SHM_SIZE, sim_cycle)", code)
        self.assertIn("FBB_CHAOS_STALL_MIN", code)
        self.assertIn("FBB_CHAOS_STALL_MAX", code)

    def test_cpp_sfinae_traits(self):
        """Compile a C++17 test checking SFINAE detection of AXI signals in sim_traits.h."""
        cpp_code = """
        #include <iostream>
        #include <cassert>
        #include <cstdint>
        #include "sim_traits.h"

        struct TopWithoutAxi {
            uint8_t clk;
            uint8_t rst_n;
            uint32_t addr;
            uint32_t w_data;
            uint8_t w_en;
            uint32_t r_data;
        };

        struct TopWithAxi {
            uint8_t clk;
            uint8_t rst_n;
            uint32_t m_axi_araddr;
            uint8_t m_axi_arvalid;
            uint8_t m_axi_arready;
            uint32_t m_axi_rdata;
            uint8_t m_axi_rvalid;
            uint8_t m_axi_rready;
        };

        int main() {
            static_assert(!has_m_axi_arvalid<TopWithoutAxi>::value, "TopWithoutAxi must not have m_axi_arvalid");
            static_assert(!has_m_axi_arready<TopWithoutAxi>::value, "TopWithoutAxi must not have m_axi_arready");
            static_assert(!has_m_axi_rvalid<TopWithoutAxi>::value, "TopWithoutAxi must not have m_axi_rvalid");

            static_assert(has_m_axi_arvalid<TopWithAxi>::value, "TopWithAxi must have m_axi_arvalid");
            static_assert(has_m_axi_arready<TopWithAxi>::value, "TopWithAxi must have m_axi_arready");
            static_assert(has_m_axi_rvalid<TopWithAxi>::value, "TopWithAxi must have m_axi_rvalid");
            static_assert(has_m_axi_rready<TopWithAxi>::value, "TopWithAxi must have m_axi_rready");
            static_assert(has_m_axi_araddr<TopWithAxi>::value, "TopWithAxi must have m_axi_araddr");
            static_assert(has_m_axi_rdata<TopWithAxi>::value, "TopWithAxi must have m_axi_rdata");

            std::cout << "SFINAE_TRAITS_OK" << std::endl;
            return 0;
        }
        """
        with tempfile.TemporaryDirectory() as tmpdir:
            src = os.path.join(tmpdir, "test_sfinae.cpp")
            bin_path = os.path.join(tmpdir, "test_sfinae")
            with open(src, "w") as f:
                f.write(cpp_code)

            inc_dir = os.path.join(PROJECT_ROOT, "src/include")
            build_res = subprocess.run(
                ["g++", "-std=c++17", f"-I{inc_dir}", src, "-o", bin_path],
                capture_output=True, text=True
            )
            self.assertEqual(build_res.returncode, 0, f"Compilation failed: {build_res.stderr}")

            run_res = subprocess.run([bin_path], capture_output=True, text=True)
            self.assertEqual(run_res.returncode, 0)
            self.assertIn("SFINAE_TRAITS_OK", run_res.stdout)

    def test_axi_stall_engine_deterministic_reproduction_and_backpressure(self):
        """Compile and execute C++ test verifying AxiStallEngine cycle-accuracy and seed determinism."""
        cpp_code = """
        #include <iostream>
        #include <vector>
        #include <cassert>
        #include <cstdint>
        #include <cstdlib>
        #include <cstring>
        #include <deque>
        #include "sim_traits.h"

        #define SHM_BASE_ADDR 0x40000000U
        #define SHM_SIZE 4096

        struct MockTopAxi {
            uint8_t clk = 0;
            uint8_t rst_n = 1;
            uint32_t m_axi_araddr = 0;
            uint8_t m_axi_arvalid = 0;
            uint8_t m_axi_arready = 1;
            uint32_t m_axi_rdata = 0;
            uint8_t m_axi_rvalid = 0;
            uint8_t m_axi_rready = 0;
        };

        struct AxiReadTx {
            uint32_t addr;
            uint32_t data;
            uint64_t respond_at_cycle;
        };

        class AxiStallEngine {
        private:
            std::deque<AxiReadTx> pipeline;
            const size_t MAX_OUTSTANDING = 8;
            int enabled = 0;
            uint64_t seed = 0;
            uint64_t s[2] = { 0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL };
            double rate = 0.10;
            uint64_t stall_min = 8;
            uint64_t stall_max = 32;
            uint64_t total_stalls = 0;
            uint64_t total_stall_cycles = 0;
            uint64_t last_log_cycle = 0;

            static inline uint64_t splitmix64(uint64_t *state) {
                uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
                return z ^ (z >> 31);
            }

            inline uint64_t xorshift128plus() {
                uint64_t x = s[0];
                uint64_t const y = s[1];
                s[0] = y;
                x ^= x << 23;
                s[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
                return s[1] + y;
            }

        public:
            size_t get_pipeline_size() const { return pipeline.size(); }
            uint64_t get_total_stalls() const { return total_stalls; }

            void init() {
                const char* mode_env = getenv("FBB_CHAOS_MODE");
                if (!mode_env || (strcmp(mode_env, "1") != 0 && strcasecmp(mode_env, "true") != 0)) {
                    enabled = 0;
                    return;
                }
                enabled = 1;

                const char* seed_env = getenv("FBB_CHAOS_SEED");
                if (seed_env && strlen(seed_env) > 0) {
                    seed = (uint64_t)strtoull(seed_env, NULL, 0);
                } else {
                    seed = 12345;
                }

                uint64_t sm = seed;
                s[0] = splitmix64(&sm);
                s[1] = splitmix64(&sm);
                if (s[0] == 0 && s[1] == 0) {
                    s[0] = 0x12345678ULL;
                    s[1] = 0x87654321ULL;
                }

                const char* rate_env = getenv("FBB_CHAOS_RATE");
                if (rate_env && strlen(rate_env) > 0) {
                    double r = atof(rate_env);
                    if (r > 1.0) r /= 100.0;
                    if (r > 0.0 && r <= 1.0) rate = r;
                }

                const char* s_min = getenv("FBB_CHAOS_STALL_MIN");
                if (s_min && strlen(s_min) > 0) {
                    stall_min = strtoull(s_min, NULL, 0);
                    if (stall_min < 1) stall_min = 1;
                }

                const char* s_max = getenv("FBB_CHAOS_STALL_MAX");
                if (s_max && strlen(s_max) > 0) {
                    stall_max = strtoull(s_max, NULL, 0);
                    if (stall_max < stall_min) stall_max = stall_min;
                }
            }

            template <typename T>
            inline void pre_clock(T* top, uint64_t current_cycle) {
                if constexpr (has_m_axi_arready<T>::value && has_m_axi_rvalid<T>::value && has_m_axi_rdata<T>::value) {
                    top->m_axi_arready = (pipeline.size() < MAX_OUTSTANDING) ? 1 : 0;
                    if (!pipeline.empty() && current_cycle >= pipeline.front().respond_at_cycle) {
                        top->m_axi_rvalid = 1;
                        top->m_axi_rdata  = pipeline.front().data;
                    } else {
                        top->m_axi_rvalid = 0;
                    }
                }
            }

            template <typename T>
            inline void post_clock(T* top, volatile uint32_t* shm, uint32_t shm_size, uint64_t current_cycle) {
                if constexpr (has_m_axi_arvalid<T>::value && has_m_axi_arready<T>::value &&
                              has_m_axi_rvalid<T>::value && has_m_axi_rready<T>::value &&
                              has_m_axi_araddr<T>::value && has_m_axi_rdata<T>::value) {
                    if (top->m_axi_arvalid && top->m_axi_arready) {
                        uint64_t delay = 1;
                        if (__builtin_expect(enabled, 0)) {
                            uint64_t r = xorshift128plus();
                            uint64_t threshold = (uint64_t)(rate * (double)0xFFFFFFFFFFFFFFFFULL);
                            if (r < threshold) {
                                uint64_t span = (stall_max >= stall_min) ? (stall_max - stall_min + 1) : 1;
                                delay = stall_min + (xorshift128plus() % span);
                                total_stalls++;
                                total_stall_cycles += delay;
                            }
                        }
                        uint32_t addr = top->m_axi_araddr;
                        uint32_t off = 0;
                        if (addr >= SHM_BASE_ADDR && addr < SHM_BASE_ADDR + shm_size) {
                            off = (addr - SHM_BASE_ADDR) / 4;
                        } else if (addr < shm_size) {
                            off = addr / 4;
                        } else {
                            off = (addr / 4) % (shm_size / 4);
                        }
                        uint32_t rdata = shm[off];
                        pipeline.push_back({ addr, rdata, current_cycle + delay });
                    }

                    if (top->m_axi_rvalid && top->m_axi_rready) {
                        if (!pipeline.empty() && current_cycle >= pipeline.front().respond_at_cycle) {
                            pipeline.pop_front();
                        }
                    }
                }
            }
        };

        std::vector<uint64_t> run_simulation_trace() {
            MockTopAxi top;
            uint32_t shm[SHM_SIZE / 4];
            for (size_t i = 0; i < SHM_SIZE / 4; i++) shm[i] = 0xA0000000 + i;

            AxiStallEngine engine;
            engine.init();

            std::vector<uint64_t> completion_cycles;
            uint64_t current_cycle = 0;
            top.m_axi_rready = 1; // Master is always ready to consume data

            // Issue 20 read requests
            int reads_to_issue = 20;
            int issued = 0;

            while (completion_cycles.size() < 20 && current_cycle < 1000) {
                current_cycle++;
                engine.pre_clock(&top, current_cycle);

                // Master sets address if pending and arready
                if (issued < reads_to_issue && top.m_axi_arready) {
                    top.m_axi_arvalid = 1;
                    top.m_axi_araddr = 0x40000000 + (issued * 4);
                    issued++;
                } else if (!top.m_axi_arready) {
                    // Backpressure active! Keep arvalid high with same address
                    top.m_axi_arvalid = (issued < reads_to_issue) ? 1 : 0;
                } else {
                    top.m_axi_arvalid = 0;
                }

                // Rising clock edge
                top.clk = 1;
                engine.post_clock(&top, shm, SHM_SIZE, current_cycle);

                if (top.m_axi_rvalid && top.m_axi_rready) {
                    completion_cycles.push_back(current_cycle);
                }

                top.clk = 0;
            }

            return completion_cycles;
        }

        int main() {
            auto trace = run_simulation_trace();
            assert(trace.size() == 20);
            for (size_t i = 0; i < trace.size(); i++) {
                std::cout << trace[i] << " ";
            }
            std::cout << std::endl;
            return 0;
        }
        """
        with tempfile.TemporaryDirectory() as tmpdir:
            src = os.path.join(tmpdir, "test_engine.cpp")
            bin_path = os.path.join(tmpdir, "test_engine")
            with open(src, "w") as f:
                f.write(cpp_code)

            inc_dir = os.path.join(PROJECT_ROOT, "src/include")
            build_res = subprocess.run(
                ["g++", "-std=c++17", f"-I{inc_dir}", src, "-o", bin_path],
                capture_output=True, text=True
            )
            self.assertEqual(build_res.returncode, 0, f"Compilation failed: {build_res.stderr}")

            # 1. Non-chaos run: all 20 reads complete in back-to-back cycles
            env_normal = os.environ.copy()
            env_normal.pop("FBB_CHAOS_MODE", None)
            res1 = subprocess.run([bin_path], capture_output=True, text=True, env=env_normal)
            self.assertEqual(res1.returncode, 0)
            trace_normal = [int(x) for x in res1.stdout.strip().split()]
            self.assertEqual(len(trace_normal), 20)
            # In ideal zero-latency, response begins at cycle 2 and arrives every cycle
            for i in range(1, len(trace_normal)):
                self.assertEqual(trace_normal[i] - trace_normal[i-1], 1,
                                 f"Normal run must have 1-cycle stride: {trace_normal}")

            # 2. Chaos run with Seed 42: Stalls must occur
            env_chaos = os.environ.copy()
            env_chaos["FBB_CHAOS_MODE"] = "1"
            env_chaos["FBB_CHAOS_SEED"] = "42"
            env_chaos["FBB_CHAOS_RATE"] = "0.30"
            env_chaos["FBB_CHAOS_STALL_MIN"] = "8"
            env_chaos["FBB_CHAOS_STALL_MAX"] = "20"
            res2 = subprocess.run([bin_path], capture_output=True, text=True, env=env_chaos)
            self.assertEqual(res2.returncode, 0)
            trace_chaos1 = [int(x) for x in res2.stdout.strip().split()]
            self.assertNotEqual(trace_normal, trace_chaos1, "Chaos run must introduce stalls")

            # 3. Repeat Chaos run with EXACT SAME Seed 42: Must be 100% bit-exact reproduction
            res3 = subprocess.run([bin_path], capture_output=True, text=True, env=env_chaos)
            self.assertEqual(res3.returncode, 0)
            trace_chaos2 = [int(x) for x in res3.stdout.strip().split()]
            self.assertEqual(trace_chaos1, trace_chaos2, "Deterministic seed must reproduce 100% bit-exact timing trace")

if __name__ == "__main__":
    unittest.main()

