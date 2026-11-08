#!/usr/bin/env python3
"""
@file scripts/v2gtr.py
@intent:responsibility
    Verilog HDL (.v) ファイルを静的解析し、Linux Device Tree (config.dts)
    および仮想ハードウェアレジスタ契約 (config.gtr) を自動生成する CLI ツール。
@intent:rationale
    初学者や外部IP持ち込み開発者が、手作業で DTS/GTR を記述することなく、
    Verilog のアドレスデコード部から即座に F-BB 先行開発環境を立ち上げられるようにする。
"""

import sys
import os
import argparse

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__))))
from vfpga.importer_verilog import VerilogImporter
from vfpga.emitter_gtr import GtrEmitter


def main():
    parser = argparse.ArgumentParser(
        description="F-BB Verilog-to-GTR Converter: Generate config.dts and config.gtr from Verilog HDL"
    )
    parser.add_argument("verilog_file", help="Path to input Verilog file (e.g., vfpga_top.v)")
    parser.add_argument("-o", "--output-dir", help="Directory where config.dts and config.gtr will be written")
    parser.add_argument("--base-addr", default="0x40000000", help="Default base address (default: 0x40000000)")
    parser.add_argument("--device-name", default=None, help="Override device name / node label (e.g., vfpga_reg)")
    parser.add_argument("--stdout", action="store_true", help="Print generated DTS and GTR to stdout")
    parser.add_argument("--table", action="store_true", help="Display discovered registers summary table")

    args = parser.parse_args()

    if not os.path.exists(args.verilog_file):
        print(f"❌ Error: Verilog file not found: {args.verilog_file}", file=sys.stderr)
        sys.exit(1)

    try:
        base_addr_val = int(args.base_addr, 0)
    except ValueError:
        print(f"❌ Error: Invalid base address: {args.base_addr}", file=sys.stderr)
        sys.exit(1)

    # 1. Verilog のパースとレジスタ抽出
    importer = VerilogImporter.from_file(
        args.verilog_file,
        default_base_addr=base_addr_val,
        device_name_override=args.device_name
    )
    devices = importer.extract_devices()

    if not devices:
        print(f"⚠️  Warning: No memory-mapped register decode found in {args.verilog_file}", file=sys.stderr)
        sys.exit(0)

    # レジスタ一覧サマリーの表示
    total_regs = sum(len(d.registers) for d in devices)
    print(f"✨ Discovered {len(devices)} device(s) and {total_regs} register(s) from {args.verilog_file}")

    if args.table or not args.output_dir and not args.stdout:
        for dev in devices:
            print(f"\n📦 Device [{dev.name}] @ {dev.base_reg}")
            print(f"   {'OFFSET':<10} {'REGISTER NAME':<24} {'DIRECTION':<10}")
            print(f"   {'-'*10} {'-'*24} {'-'*10}")
            for reg in dev.registers:
                print(f"   {reg.offset:<10} {reg.name:<24} {reg.direction:<10}")

    # 出力処理
    if args.output_dir:
        dts_p, gtr_p = GtrEmitter.write_scenario_pair(devices, args.output_dir)
        print(f"\n💾 Successfully generated:")
        print(f"   - DTS: {dts_p}")
        print(f"   - GTR: {gtr_p}")

    if args.stdout:
        print("\n" + "=" * 60)
        print("--- config.dts ---")
        print(GtrEmitter.emit_dts(devices))
        print("--- config.gtr ---")
        print(GtrEmitter.emit_gtr(devices))
        print("=" * 60)


if __name__ == "__main__":
    main()

