#!/usr/bin/env python3
"""
@file scripts/vfpga/importer_verilog.py
@intent:responsibility
    Verilog HDL (.v) ファイルを静的解析し、アドレスデコード回路、読出し/書込みロジック、
    パラメータ、コメント注記からメモリマップドレジスタ仕様を抽出して Device / Register モデルを構築する。
@intent:rationale
    外部IPや既存の Verilog 回路から、F-BB 標準の config.dts および config.gtr を
    自動生成するための抽象中間表現（IR: Intermediate Representation）を生成する。
@intent:pre-condition
    入力はメモリマップドバス（AXI-Lite または F-BB vfpga_top バス）のアドレスデコードを含む Verilog ソースコードであること。
"""

import os
import re
from vfpga.models import Device, Register


class VerilogImporter:
    """
    Verilog ソースコードからレジスタ定義を抽出するパーサークラス
    """

    def __init__(self, verilog_content: str, default_base_addr: int = 0x40000000, device_name_override: str = None):
        self.raw_content = verilog_content
        self.default_base_addr = default_base_addr
        self.device_name_override = device_name_override

    @staticmethod
    def parse_number(val_str: str) -> int:
        """
        Verilog 数値表記（32'h40000010, 'h10, 0x10, 32'd16, 16 等）を整数値に変換する
        """
        s = val_str.replace('_', '').strip()
        lower = s.lower()
        if "'h" in lower:
            return int(lower.split("'h")[1], 16)
        elif "'d" in lower:
            return int(lower.split("'d")[1], 10)
        elif "'b" in lower:
            return int(lower.split("'b")[1], 2)
        elif lower.startswith(("0x", "0X")):
            return int(lower, 16)
        else:
            return int(lower, 0)

    @classmethod
    def from_file(cls, filepath: str, default_base_addr: int = 0x40000000, device_name_override: str = None):
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        return cls(content, default_base_addr, device_name_override)

    def extract_devices(self) -> list:
        """
        Verilog コードから Device リストを抽出して返す
        """
        # 1. モジュール名の取得
        mod_match = re.search(r'\bmodule\s+([a-zA-Z0-9_]+)', self.raw_content)
        module_name = mod_match.group(1) if mod_match else "vfpga_reg"
        device_label = self.device_name_override or (
            "vfpga_reg" if module_name == "vfpga_top" else module_name
        )

        # 2. 定数 (localparam / parameter) のマッピング
        param_map = {}
        for p_match in re.finditer(
            r'\b(?:localparam|parameter)\s+(?:(?:integer|reg|wire|\[[^\]]+\])\s+)*([a-zA-Z0-9_]+)\s*=\s*([^;]+);',
            self.raw_content
        ):
            p_name = p_match.group(1).strip()
            p_val_str = p_match.group(2).strip()
            try:
                param_map[p_name] = self.parse_number(p_val_str)
            except Exception:
                pass

        # 3. コメント行からのレジスタヒント抽出
        # 例: // 0x40000000: REG_SPI_TX (RW)
        # 例: // 32'h40000004: REG_SPI_RX (RO)
        # 例: // Dummy GPIO DATA register output (offset 0x00)
        comment_hints = {}
        for c_match in re.finditer(r'//\s*(?:(?:32\'h|0x)?([0-9a-fA-F]+))\s*:\s*([A-Za-z0-9_]+)(?:\s*\(([A-Za-z0-9_/]+)\))?', self.raw_content):
            c_addr_hex = c_match.group(1)
            c_name = c_match.group(2).strip()
            c_dir = c_match.group(3).strip().upper() if c_match.group(3) else None
            try:
                c_addr = int(c_addr_hex, 16)
                comment_hints[c_addr] = {'name': c_name, 'direction': c_dir}
            except Exception:
                pass

        # 4. case 文 (アドレスデコード部) の走査
        # case (addr) ... endcase
        write_entries = {}  # addr -> signal_name
        read_entries = {}   # addr -> signal_name

        case_blocks = re.finditer(r'\bcase\s*\(([^)]+)\)(.*?)endcase', self.raw_content, re.DOTALL)
        for cb in case_blocks:
            selector = cb.group(1).strip()
            body = cb.group(2)

            # アドレスビットスライスの検出 (例: addr[ADDR_LSB+OPT_MEM_ADDR_BITS:ADDR_LSB] or addr[7:2])
            shift_mult = 1
            slice_match = re.search(r'\[[^:]+:([a-zA-Z0-9_]+)\]', selector)
            if slice_match:
                lsb_token = slice_match.group(1).strip()
                if lsb_token.isdigit():
                    shift_mult = 1 << int(lsb_token)
                elif lsb_token in param_map:
                    shift_mult = 1 << param_map[lsb_token]
                elif lsb_token.upper() in param_map:
                    shift_mult = 1 << param_map[lsb_token.upper()]
                elif lsb_token.lower() in param_map:
                    shift_mult = 1 << param_map[lsb_token.lower()]

            # 書込み系か読出し系かの判定 (大文字小文字を問わず判定)
            is_write_case = bool(re.search(r'<=\s*(?:w_data|s_axi_wdata|s_axi_wdata\[)', body, re.IGNORECASE) or
                                 re.search(r'\b(?:w_en|wvalid|awvalid|s_axi_wvalid|s_axi_awvalid)\b', body, re.IGNORECASE))
            is_read_case = bool(re.search(r'\b(?:r_data|s_axi_rdata)\s*(?:<=|=)\s*', body, re.IGNORECASE))

            # case アイテムの抽出 (例: 32'h40000010: RST <= w_data;)
            item_matches = re.finditer(r'([0-9a-zA-Z_\'h,\s]+)\s*:\s*([^;\n]+(?:;[^\n]*)?)', body)
            for item in item_matches:
                labels_str = item.group(1).strip()
                action_str = item.group(2).strip()

                if labels_str.lower() == "default":
                    continue

                # インラインコメントの解析 (例: // Dummy GPIO DATA register output)
                inline_hint_name = None
                if '//' in action_str:
                    comment_part = action_str.split('//', 1)[1]
                    words = re.findall(r'\b[A-Z][A-Z0-9_]{1,16}\b', comment_part)
                    valid_words = [w for w in words if w not in ['DUMMY', 'GPIO', 'AXI', 'UIO', 'REG', 'REGISTER', 'OUTPUT', 'INPUT', 'ALL']]
                    if valid_words:
                        inline_hint_name = valid_words[0]

                for lbl in labels_str.split(','):
                    lbl_clean = lbl.strip()
                    if not lbl_clean:
                        continue

                    # ラベルのアドレス解決
                    addr_val = None
                    if lbl_clean in param_map:
                        addr_val = param_map[lbl_clean] * shift_mult
                    else:
                        try:
                            addr_val = self.parse_number(lbl_clean) * shift_mult
                        except Exception:
                            continue

                    if addr_val is None:
                        continue

                    # アクションからシグナル名を特定
                    signal_name = None
                    if is_write_case:
                        assign_match = re.search(r'([a-zA-Z0-9_]+)\s*<=', action_str)
                        if assign_match:
                            signal_name = assign_match.group(1).strip()
                    if is_read_case:
                        # r_data = RST; や r_data = {24'd0, r_spi_tx}; から信号名を抽出
                        rhs_match = re.search(r'(?:r_data|s_axi_rdata)\s*(?:<=|=)\s*([^;]+)', action_str, re.IGNORECASE)
                        if rhs_match:
                            rhs = rhs_match.group(1)
                            # 数値リテラルを除去し、識別子を抽出
                            tokens = re.findall(r'\b[a-zA-Z_][a-zA-Z0-9_]*\b', rhs)
                            valid_tokens = [t for t in tokens if not re.match(r'^(?:deadbeef|d0|h0)$', t.lower())]
                            if valid_tokens:
                                signal_name = valid_tokens[-1]

                    if is_write_case:
                        write_entries[addr_val] = signal_name
                    if is_read_case:
                        read_entries[addr_val] = signal_name
                    if inline_hint_name and addr_val not in comment_hints:
                        comment_hints[addr_val] = {'name': inline_hint_name, 'direction': None}

        # 4b. if 文 (アドレス比較) による書込みデコードの走査
        # 例: if (addr == 32'h40000000) reg_ctrl <= w_data;
        # 例: if (w_en && addr == 32'h40000008 && w_data == 32'h1) reg_status[0] <= 1'b0; // ACK
        if_write_matches = re.finditer(
            r'\bif\s*\([^)]*?(?:(?:addr|s_axi_awaddr|s_axi_araddr)\s*==\s*([0-9a-zA-Z_\'h]+)|([0-9a-zA-Z_\'h]+)\s*==\s*(?:addr|s_axi_awaddr|s_axi_araddr))[^)]*\)\s*(?:begin\s*)?([^\n;]+;[^\n]*)',
            self.raw_content
        )
        for iw in if_write_matches:
            addr_str = iw.group(1) or iw.group(2)
            stmt = iw.group(3).strip()

            inline_hint_name = None
            if '//' in stmt:
                words = [w for w in re.findall(r'\b[A-Z][A-Z0-9_]{1,16}\b', stmt.split('//', 1)[1]) if w not in ['DUMMY', 'GPIO', 'AXI', 'UIO', 'REG', 'REGISTER', 'OUTPUT', 'INPUT', 'ALL']]
                if words:
                    inline_hint_name = words[0]

            addr_val = None
            if addr_str in param_map:
                addr_val = param_map[addr_str]
            else:
                try:
                    addr_val = self.parse_number(addr_str)
                except Exception:
                    continue

            if addr_val is None:
                continue

            assign_match = re.search(r'([a-zA-Z0-9_]+)(?:\[[^\]]+\])?\s*<=', stmt)
            if assign_match:
                sig_name = assign_match.group(1).strip()
                if addr_val not in write_entries:
                    write_entries[addr_val] = sig_name
                if inline_hint_name and addr_val not in comment_hints:
                    comment_hints[addr_val] = {'name': inline_hint_name, 'direction': None}

        # 4c. assign による三項演算子読出し (Ternary Read Multiplexer) の走査
        # 例: assign r_data = (addr == 32'h40000004) ? sub_out : reg0;
        assign_read_matches = re.finditer(
            r'\bassign\s+(?:r_data|s_axi_rdata)\s*=\s*([^;]+);',
            self.raw_content,
            re.IGNORECASE
        )
        for ar in assign_read_matches:
            expr = ar.group(1)
            ternary_matches = re.finditer(
                r'(?:\((?:[^\)]*?\b)?(?:addr|s_axi_araddr)\s*==\s*([0-9a-zA-Z_\'h]+)[^)]*\)|(?:addr|s_axi_araddr)\s*==\s*([0-9a-zA-Z_\'h]+))\s*\?\s*([a-zA-Z0-9_]+)',
                expr
            )
            for tm in ternary_matches:
                addr_str = tm.group(1) or tm.group(2)
                sig = tm.group(3).strip()
                addr_val = None
                if addr_str in param_map:
                    addr_val = param_map[addr_str]
                else:
                    try:
                        addr_val = self.parse_number(addr_str)
                    except Exception:
                        continue
                if addr_val is not None:
                    read_entries[addr_val] = sig

            # Check default / fallback signal in the ternary chain (e.g. ": reg0")
            colons = expr.split(':')
            if len(colons) > 1:
                for fbt in re.findall(r'\b[a-zA-Z_][a-zA-Z0-9_]*\b', colons[-1].strip()):
                    if fbt.lower() not in ('h0', 'd0', 'deadbeef', 'default', '32'):
                        for w_addr, w_sig in write_entries.items():
                            if w_sig == fbt:
                                read_entries[w_addr] = w_sig

        all_addrs = sorted(set(list(write_entries.keys()) + list(read_entries.keys()) + list(comment_hints.keys())))
        if not all_addrs:
            return []

        # 5. アドレスクラスタリングによるデバイス分離
        # アドレスが 4KB (0x1000) 単位で離れている場合は別デバイス（ペリフェラル）として認識
        dev_groups = {}
        for addr in all_addrs:
            # アドレスが十分に大きい場合は 4KB 境界でクラスタリング
            if addr >= 0x10000:
                base = addr & ~0xFFF
            else:
                base = self.default_base_addr
            dev_groups.setdefault(base, []).append(addr)

        devices = []
        dev_idx = 0
        for base_addr, addrs in sorted(dev_groups.items()):
            current_label = device_label if len(dev_groups) == 1 else f"{device_label}_{dev_idx}"
            if len(dev_groups) > 1:
                # 複数デバイス時の名前推測ヒント
                if base_addr == 0x40000000 and "spi" in self.raw_content.lower():
                    current_label = "pl_spi_ctrl"
                elif base_addr == 0x40002000 and "gpio" in self.raw_content.lower():
                    current_label = "vfpga_gpio"

            dev_obj = Device(
                name=current_label,
                path=f"/dev/uio{dev_idx}",
                dev_type="uio",
                base_reg=f"0x{base_addr:08x} 0x1000"
            )

            # レジスタオブジェクトの構築
            for addr in addrs:
                offset = addr - base_addr if addr >= base_addr else addr
                hint = comment_hints.get(addr, {})

                # レジスタ名の決定
                reg_name = None
                if hint.get('name'):
                    reg_name = hint['name']
                else:
                    sig = write_entries.get(addr) or read_entries.get(addr)
                    if sig:
                        # プレフィックスの整理 (r_spi_tx -> SPI_TX, slv_reg0 -> REG0)
                        clean_sig = re.sub(r'^(?:r_|reg_|s_)', '', sig)
                        clean_sig = re.sub(r'^slv_reg', 'REG', clean_sig)
                        reg_name = clean_sig.upper()
                    else:
                        reg_name = f"REG_{offset:02X}"

                # アクセス権限の決定
                reg_dir = "RW"
                if hint.get('direction'):
                    reg_dir = hint['direction']
                elif addr in write_entries and addr not in read_entries:
                    reg_dir = "WO"
                elif addr in read_entries and addr not in write_entries:
                    reg_dir = "RO"
                else:
                    reg_dir = "RW"

                reg_obj = Register(
                    name=reg_name,
                    offset=f"0x{offset:02x}",
                    direction=reg_dir
                )
                dev_obj.registers.append(reg_obj)

            devices.append(dev_obj)
            dev_idx += 1

        return devices
