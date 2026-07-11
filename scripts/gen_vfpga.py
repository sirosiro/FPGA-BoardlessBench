#!/usr/bin/env python3
import sys
import os

# Ensure the scripts directory is in sys.path so the vfpga package can be imported
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Expose package classes for backward compatibility (Facade)
from vfpga.models import Device, Register, I2CSlave, BoardModel
from vfpga.parser import DTSParser
from vfpga.generator_base import BaseGenerator, ConfigGenerator
from vfpga.generator_shim import ShimGenerator
from vfpga.generator_rtl import RTLGenerator, SimulatorGenerator, ManifestGenerator, RustPACGenerator
from vfpga.orchestrator import GeneratorOrchestrator

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(1)
    model = DTSParser.parse(sys.argv[1])
    GeneratorOrchestrator(model, sys.argv[1]).generate_all()
