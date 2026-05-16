CC = gcc
CFLAGS = -Wall -Wextra -fPIC -Isrc/include
LDFLAGS = -shared -ldl

SHIM_SRC = src/shim/libfpgashim.c
SHIM_OUT = libfpgashim.so
GEN_SCRIPT = scripts/gen_vfpga.py
# RTL Sources: Check if the scenario provides its own vfpga_top.v
SCENARIO_DIR ?=
ifneq ($(wildcard $(SCENARIO_DIR)/vfpga_top.v),)
    RTL_TOP = $(SCENARIO_DIR)/vfpga_top.v
else
    RTL_TOP = src/rtl/vfpga_top.v
endif

# Collect all scenario-specific Verilog files (excluding vfpga_top.v if already picked)
SCENARIO_V_FILES = $(filter-out $(RTL_TOP), $(wildcard $(SCENARIO_DIR)/*.v))

# Final RTL Source list
RTL_SRCS = $(RTL_TOP) $(SCENARIO_V_FILES)

# Verilator
VERILATOR = verilator
VERILATOR_ROOT = /usr/share/verilator
VERILATOR_FLAGS = -Wall --cc --trace -CFLAGS "-I../src/include -std=c++17"
SIM_SRC = src/sim/sim_main.cpp
SIM_OUT = vfpga_sim

all: engine

# Build the Interception Shim
$(SHIM_OUT): $(SHIM_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

# Build the Verilator Simulator (Decoupled from Verilator's internal C++ parser)
$(SIM_OUT): $(RTL_SRCS) $(SIM_SRC)
	$(VERILATOR) $(VERILATOR_FLAGS) $(RTL_SRCS)
	$(MAKE) -C obj_dir -f Vvfpga_top.mk
	g++ -std=c++17 -Isrc/include -I$(VERILATOR_ROOT)/include -Iobj_dir \
		$(VERILATOR_ROOT)/include/verilated.cpp \
		$(VERILATOR_ROOT)/include/verilated_vcd_c.cpp \
		$(SIM_SRC) obj_dir/Vvfpga_top__ALL.a -o $(SIM_OUT)

engine: $(SHIM_OUT) $(SIM_OUT)

clean:
	rm -f $(SHIM_OUT)
	rm -f vfpga_sim
	rm -f vfpga.vcd
	rm -f $(SHIM_SRC) src/include/vfpga_config.h
	rm -f src/sim/sim_main.cpp src/rtl/*.v
	rm -rf obj_dir
	$(MAKE) -C tests clean || true
	$(MAKE) -C sandbox clean || true

# Helper for Docker
docker-up:
	docker compose up --build

docker-down:
	docker compose down

.PHONY: all engine clean verilate docker-up docker-down
