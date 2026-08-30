"""
@file scripts/vfpga/parser.py
@intent:responsibility
    Device Tree Source (config.dts) を構文解析し、F-BB の内部オブジェクトモデル（BoardModel, Device, Register, I2CSlave, SPISlave）を構築する。
    構文誤記やインクルード欠落を検知した際は、行番号・コードスニペット・修正ヒントを含む親切な DTSParserError を発行する。
@intent:rationale
    F-BB では Single Source of Truth（SSOT）原則（ADR #001）を徹底しており、C-Shim、Verilog スケルトン、
    シミュレータラッパー、Rust PAC、ダッシュボードマニフェスト（board_manifest.json）の全生成物は、
    この DTS パーサーが生成する単一の BoardModel から一元的に導出される。
    初学者が DTS のセミコロン抜けや中括弧不整合を起こした際にも、即座に修正箇所を特定できるようにする。
@intent:pre-condition
    入力ファイルは有効な DTS 構文であり、F-BB の独自拡張プロパティ（registers @ 0x00:RW, fbb,mock-data 等）を含みうること。
"""

import re
import os
from vfpga.models import Device, Register, I2CSlave, SPISlave, BoardModel


class DTSParserError(Exception):
    """
    @class DTSParserError
    @intent:responsibility
        DTS 構文解析エラー時に、ファイル名、行番号、該当コードスニペット、原因、および修正ヒントを整形して通知する。
    """
    def __init__(self, message, file_path=None, line=None, node=None, snippet=None, tip=None):
        self.message = message
        self.file_path = file_path
        self.line = line
        self.node = node
        self.snippet = snippet
        self.tip = tip
        super().__init__(self._format_error())

    def _format_error(self):
        lines = [
            "=" * 70,
            "[DTSParser Error] Syntax error in Device Tree Source",
            "-" * 70,
        ]
        if self.file_path:
            lines.append(f"File     : {self.file_path}")
        if self.line:
            lines.append(f"Line     : {self.line}")
        if self.node:
            lines.append(f"Node     : {self.node}")
        if self.snippet:
            lines.append("Snippet  :")
            for s in self.snippet.splitlines():
                lines.append(f"  {s}")
        lines.append(f"Reason   : {self.message}")
        if self.tip:
            lines.append(f"Tip      : {self.tip}")
        lines.append("=" * 70)
        return "\n".join(lines)


class DTSParser:
    """
    @class DTSParser
    @intent:responsibility
        DTS ファイルの再帰的 include 展開、マクロ展開、構文バリデーション、ノード抽出、レジスタ権限・ペリフェラル属性のパースを担当。
    @intent:rationale
        外部の巨大な dtc（Device Tree Compiler）バイナリに依存せず、Python 標準ライブラリのみで高速・ポータブルに
        DTS サブセットをパースし、親切な診断レポートを出力することで、ゼロ依存の高速起動と優れた開発体験を両立する。
    """

    @staticmethod
    def make_snippet(content, line_num, radius=2):
        """
        @intent:responsibility
            エラー発生行の前後数行を抽出し、問題の行に '>' ポインタを付与した行番号付きスニペットを生成する。
        @intent:rationale
            単なるエラー行番号だけでなく前後のコードコンテキストを可視化することで、初学者がエラー箇所を一目で把握できるようにする。
        @intent:pre-condition
            content は改行区切りの文字列であり、line_num は 1 以上の整数であること。
        """
        lines = content.splitlines()
        start_idx = max(0, line_num - radius - 1)
        end_idx = min(len(lines), line_num + radius)
        res = []
        for idx in range(start_idx, end_idx):
            cur_line = idx + 1
            prefix = "> " if cur_line == line_num else "  "
            res.append(f"{prefix}{cur_line:4d} | {lines[idx]}")
        return "\n".join(res)

    @staticmethod
    def _remove_comments_and_strings(text):
        """
        @intent:responsibility
            コメント（/* ... */, // ...）および文字列リテラル（"..."）を行数・文字位置を崩さずに空白でマスクする。
        @intent:rationale
            コメントや文字列の中に含まれる '{', '}' などの記号が中括弧バランス検査を誤認させるのを完全に防ぐ。
        @intent:pre-condition
            text は有効な UTF-8 文字列であること。
        """
        def repl_str(m):
            return '"' + ' ' * (len(m.group(0)) - 2) + '"'
        text_no_str = re.sub(r'"([^"\\]|\\.)*"', repl_str, text)
        def repl_block(m):
            return '\n' * m.group(0).count('\n')
        text_no_comm = re.sub(r'/\*.*?\*/', repl_block, text_no_str, flags=re.DOTALL)
        return re.sub(r'//.*$', '', text_no_comm, flags=re.MULTILINE)

    @staticmethod
    def validate_syntax_precheck(content, file_path):
        """
        @intent:responsibility
            DTS の中括弧不整合（閉じ忘れ・余剰）およびプロパティ末尾のセミコロン忘れを事前に静的検査する。
        @intent:rationale
            ジェネレーターや正規表現パーサーが中途半端に失敗する前に、明確な行番号と修正 Tip を添えて早期に診断する。
        @intent:pre-condition
            content はファイルから読み込まれた生の DTS 文字列であること。
        """
        # 1. Check balanced braces
        clean = DTSParser._remove_comments_and_strings(content)
        open_braces = []
        for idx, char in enumerate(clean):
            if char == '{':
                open_braces.append(idx)
            elif char == '}':
                if open_braces:
                    open_braces.pop()
                else:
                    line_num = content[:idx].count('\n') + 1
                    raise DTSParserError(
                        "Extraneous closing brace '}' without matching '{'",
                        file_path=file_path,
                        line=line_num,
                        snippet=DTSParser.make_snippet(content, line_num),
                        tip="Check for duplicate or misplaced '};' node closers."
                    )
        if open_braces:
            last_open_idx = open_braces[-1]
            line_num = content[:last_open_idx].count('\n') + 1
            raise DTSParserError(
                "Unclosed opening brace '{' detected (missing matching '};')",
                file_path=file_path,
                line=line_num,
                snippet=DTSParser.make_snippet(content, line_num),
                tip="Ensure every opened node '{' has a corresponding closing '};'."
            )

        # 2. Check property semicolons
        lines = content.splitlines()
        in_block_comment = False
        in_property = False
        prop_start_line = 0

        for idx, line in enumerate(lines):
            line_num = idx + 1
            stripped = line.strip()
            if not stripped:
                continue

            if "/*" in line:
                in_block_comment = True
            if "*/" in line:
                in_block_comment = False
                continue
            if in_block_comment or stripped.startswith("//") or stripped.startswith("#"):
                continue

            if re.match(r'^[a-zA-Z0-9_,-]+\s*=', stripped) and not stripped.endswith('{'):
                if in_property:
                    raise DTSParserError(
                        "Missing terminating ';' in property definition",
                        file_path=file_path,
                        line=prop_start_line,
                        snippet=DTSParser.make_snippet(content, prop_start_line),
                        tip="Each property assignment must end with a semicolon (e.g. 'label = \"/dev/uio0\";')."
                    )
                in_property = True
                prop_start_line = line_num

            if in_property:
                if stripped.endswith(';'):
                    in_property = False
                elif stripped.endswith('{') or (re.match(r'^[a-zA-Z0-9_@:-]+\s*\{', stripped) and not stripped.startswith('registers')):
                    raise DTSParserError(
                        "Missing terminating ';' before new node definition",
                        file_path=file_path,
                        line=prop_start_line,
                        snippet=DTSParser.make_snippet(content, prop_start_line),
                        tip="Each property assignment must end with a semicolon before starting a child node."
                    )

        if in_property:
            raise DTSParserError(
                "Missing terminating ';' in property definition",
                file_path=file_path,
                line=prop_start_line,
                snippet=DTSParser.make_snippet(content, prop_start_line),
                tip="Ensure the last property definition ends with ';'."
            )

    @staticmethod
    def preprocess_includes(content, base_dir, visited=None, source_file=None):
        """
        @intent:responsibility
            #include ディレクティブを再帰的に走査し、インクルード先ファイルの内容でインライン置換する。
        @intent:rationale
            共通 DTS ヘッダー（例: #include "zynq-7000.dtsi"）や共有定義を再利用可能にし、シナリオごとの重複記述を排除する。
        @intent:pre-condition
            循環インクルードを防止するため visited セットで探索履歴を追跡する。
        """
        if visited is None:
            visited = set()

        def replace_inc(match):
            inc_path = match.group(1).strip()
            # Try resolving relative to base_dir
            target_path = os.path.normpath(os.path.join(base_dir, inc_path))
            if not os.path.exists(target_path):
                # Try relative to PROJECT_ROOT
                proj_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
                target_path = os.path.normpath(os.path.join(proj_root, inc_path))

            if not os.path.exists(target_path):
                match_start = match.start()
                line_num = content[:match_start].count('\n') + 1
                raise DTSParserError(
                    f"Included file not found: '{inc_path}'",
                    file_path=source_file,
                    line=line_num,
                    snippet=DTSParser.make_snippet(content, line_num),
                    tip=f"Check if '{inc_path}' exists in the scenario directory or in the repository root."
                )

            if target_path in visited:
                return ""  # Avoid circular inclusion
            visited.add(target_path)

            try:
                with open(target_path, "r", encoding="utf-8") as f_inc:
                    inc_content = f_inc.read()
                return DTSParser.preprocess_includes(inc_content, os.path.dirname(target_path), visited, target_path)
            except DTSParserError:
                raise
            except Exception as e:
                match_start = match.start()
                line_num = content[:match_start].count('\n') + 1
                raise DTSParserError(
                    f"Error reading include file '{target_path}': {e}",
                    file_path=source_file,
                    line=line_num,
                    snippet=DTSParser.make_snippet(content, line_num),
                    tip="Ensure the file has read permissions and is valid UTF-8 text."
                )

        # Match #include "..." or #include <...>
        pattern = r'#include\s+["<]([^">]+)[">]'
        return re.sub(pattern, replace_inc, content)

    @staticmethod
    def find_matching_braces(text, start_pos):
        """
        @intent:responsibility
            DTS ノードの開始中括弧 '{' に対応する閉じ中括弧 '}' のインデックス範囲を正確に検出する。
        @intent:rationale
            DTS のネストされたサブノード（I2C/SPI スレーブ等）を正しく抽出し、プロパティの誤適用を防ぐ。
        """
        brace_pos = text.find("{", start_pos)
        if brace_pos == -1:
            return -1, -1
        count = 1
        i = brace_pos + 1
        while i < len(text) and count > 0:
            if text[i] == "{":
                count += 1
            elif text[i] == "}":
                count -= 1
            i += 1
        if count == 0:
            return brace_pos, i
        return -1, -1

    @staticmethod
    def parse(dts_path):
        """
        @intent:responsibility
            指定された config.dts をパースし、メモリマップ、ペリフェラル種別、レジスタアクセス権を含む BoardModel を構築・返却する。
        @intent:rationale
            単一のパース結果からすべてのシミュレーション・Shim・RTL・UI コンポーネントを整合性を保って自動生成する。
        @intent:pre-condition
            dts_path が指すファイルが存在し、読み取り可能であること。
        """
        if not os.path.exists(dts_path):
            raise DTSParserError(
                f"Device Tree Source file not found: '{dts_path}'",
                file_path=dts_path,
                tip="Check if the file path is correct or scaffold a new scenario using 'bin/fbb new'."
            )

        with open(dts_path, "r", encoding="utf-8") as f:
            raw_content = f.read()

        # Precheck syntax on the primary DTS file before expansion
        DTSParser.validate_syntax_precheck(raw_content, dts_path)

        # Preprocess #include statements recursively
        content = DTSParser.preprocess_includes(raw_content, os.path.dirname(os.path.abspath(dts_path)), source_file=dts_path)

        # Expand #define macros (e.g. #define FBB_I2C_BUS i2c0)
        defines = dict(re.findall(r"#define\s+([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)", content))
        for k, v in defines.items():
            content = re.sub(r"\b" + re.escape(k) + r"\b", v, content)

        # Extract root-level compatible and model BEFORE trimming content
        compatible_bytes = b"generic,fbb-vfpga\x00"
        root_compat_match = re.search(r"/\s*\{[^{]*?compatible\s*=\s*([^;]+);", content, re.DOTALL)
        if root_compat_match:
            parts = re.findall(r'"([^"]+)"', root_compat_match.group(1))
            compatible_bytes = b"".join([p.encode("utf-8") + b"\x00" for p in parts if p])

        model_name = "generic-vfpga"
        root_model_match = re.search(r'/\s*\{[^{]*?model\s*=\s*"([^"]+)";', content, re.DOTALL)
        if root_model_match:
            model_name = root_model_match.group(1).strip()

        # Extract and merge top-level &label { ... } node references into root content
        ref_matches = list(re.finditer(r"&[a-zA-Z0-9_]+\s*\{", content))
        ref_contents = []
        for rmatch in reversed(ref_matches):
            b_start, b_end = DTSParser.find_matching_braces(content, rmatch.start())
            if b_start != -1:
                label_name = rmatch.group(0).split("{")[0].strip("& \t")
                block_body = content[b_start + 1 : b_end - 1]
                ref_contents.append((label_name, block_body))
                content = content[: rmatch.start()] + content[b_end:]

        devices = []

        # 1. Look for root node '/'
        root_match = re.search(r"/\s*\{", content)
        if root_match:
            brace_start, brace_end = DTSParser.find_matching_braces(content, root_match.start())
            if brace_start != -1:
                content = content[brace_start + 1 : brace_end - 1]

        # Merge reference node contents into matching label nodes
        for label_name, block_body in ref_contents:
            lbl_pattern = r"(" + re.escape(label_name) + r"\s*:\s*[a-zA-Z0-9_@:-]+\s*\{)"
            m_lbl = re.search(lbl_pattern, content)
            if m_lbl:
                b_start, b_end = DTSParser.find_matching_braces(content, m_lbl.start())
                if b_start != -1:
                    content = content[: b_end - 1] + "\n" + block_body + "\n" + content[b_end - 1 :]

        pos = 0
        while True:
            # Match top-level nodes (e.g. node@1000 or label: node@1000)
            match = re.search(r"([a-zA-Z0-9_@:-]+)\s*\{", content[pos:])
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
            label_prefix = None
            if ":" in raw_name:
                parts = raw_name.split(":")
                label_prefix = parts[0].strip()
                node_name = parts[-1].strip()

            name = label_prefix if label_prefix else node_name.split("@")[0]

            # Clean body by removing nested sub-node blocks to prevent property overriding
            clean_body = body
            while True:
                sub_match = re.search(r"([a-zA-Z0-9_@:-]+)\s*\{", clean_body)
                if not sub_match:
                    break
                sub_start = sub_match.start()
                sub_brace_start, sub_brace_end = DTSParser.find_matching_braces(clean_body, sub_start)
                if sub_brace_start == -1:
                    break
                clean_body = clean_body[:sub_start] + clean_body[sub_brace_end:]

            props = {}
            prop_matches = re.finditer(r"([a-zA-Z0-9_,-]+)\s*=\s*([^;]+);", clean_body)
            for p_match in prop_matches:
                k = p_match.group(1).strip()
                v = p_match.group(2).strip()
                if "{" in v or "}" in v:
                    continue
                if v.startswith("<") and v.endswith(">"):
                    v = v[1:-1].strip()
                if v.startswith('"') and v.endswith('"'):
                    v = v[1:-1].strip()
                props[k] = v

            if "compatible" in props:
                compatible = props.get("compatible", "")
                label = props.get("label", "/dev/%s" % name)
                dev_type = "unknown"
                if "generic-uio" in compatible:
                    dev_type = "uio"
                elif "i2c" in compatible or "cdns,i2c" in compatible:
                    dev_type = "i2c"
                elif "uart" in compatible or "xlnx,xps-uart" in compatible:
                    dev_type = "uart"
                elif "gpio" in compatible or "xlnx,xps-gpio" in compatible:
                    dev_type = "gpio"
                elif "spi" in compatible or "cdns,spi" in compatible or "xlnx,zynq-spi" in compatible:
                    dev_type = "spi"
                elif "dma" in compatible or "xlnx,axi-dma" in compatible or "xlnx,axi-cdma" in compatible:
                    dev_type = "dma"
                elif "rpmsg" in compatible:
                    dev_type = "rpmsg"
                elif any(c in compatible for c in ["xlnx,zynq-can", "bosch,cc770", "nxp,flexcan", "fbb,can"]):
                    dev_type = "can"
                if dev_type == "unknown" and label.startswith("/dev/uio"):
                    dev_type = "uio"

                reg_val = props.get("reg", "0x0 0x0")
                device = Device(name, label, dev_type, reg_val)
                for k, v in props.items():
                    if k not in ["label", "reg", "registers"]:
                        device.extra_props[k] = v

                # Parse nested I2C slave devices
                if dev_type == "i2c":
                    sub_pos = 0
                    while True:
                        sub_match = re.search(r"([a-zA-Z0-9_@:-]+)\s*\{", body[sub_pos:])
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
                        if ":" in sub_raw_name:
                            s_node_name = sub_raw_name.split(":")[-1].strip()

                        s_name = s_node_name.split("@")[0]
                        s_addr_str = s_node_name.split("@")[1] if "@" in s_node_name else "0"
                        try:
                            s_addr = int(s_addr_str, 16)
                        except:
                            s_addr = 0

                        s_props = {}
                        s_prop_matches = re.finditer(r"([a-zA-Z0-9_,-]+)\s*=\s*([^;]+);", sub_body)
                        for sp_match in s_prop_matches:
                            sk = sp_match.group(1).strip()
                            sv = sp_match.group(2).strip()
                            if sv.startswith("<") and sv.endswith(">"):
                                sv = sv[1:-1].strip()
                            if sv.startswith('"') and sv.endswith('"'):
                                sv = sv[1:-1].strip()
                            s_props[sk] = sv

                        if "compatible" in s_props:
                            init_val_str = s_props.get("fbb,mock-data", "0x10")
                            try:
                                init_val = int(init_val_str, 0)
                            except:
                                init_val = 0x10
                            mock_file = s_props.get("fbb,mock-file", None)
                            slave = I2CSlave(s_name, s_addr, s_props["compatible"], mock_file, init_val)
                            device.i2c_slaves.append(slave)

                # Parse nested SPI slave devices
                if dev_type == "spi":
                    sub_pos = 0
                    while True:
                        sub_match = re.search(r"([a-zA-Z0-9_@:-]+)\s*\{", body[sub_pos:])
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
                        if ":" in sub_raw_name:
                            s_node_name = sub_raw_name.split(":")[-1].strip()

                        s_name = s_node_name.split("@")[0]
                        s_cs_str = s_node_name.split("@")[1] if "@" in s_node_name else "0"
                        try:
                            s_cs = int(s_cs_str, 0)
                        except:
                            s_cs = 0

                        s_props = {}
                        s_prop_matches = re.finditer(r"([a-zA-Z0-9_,-]+)\s*=\s*([^;]+);", sub_body)
                        for sp_match in s_prop_matches:
                            sk = sp_match.group(1).strip()
                            sv = sp_match.group(2).strip()
                            if sv.startswith("<") and sv.endswith(">"):
                                sv = sv[1:-1].strip()
                            if sv.startswith('"') and sv.endswith('"'):
                                sv = sv[1:-1].strip()
                            s_props[sk] = sv

                        if "compatible" in s_props:
                            init_val_str = s_props.get("fbb,mock-data", "2048")
                            try:
                                init_val = int(init_val_str, 0)
                            except:
                                init_val = 2048
                            mock_file = s_props.get("fbb,mock-file", None)
                            slave = SPISlave(s_name, s_cs, s_props["compatible"], mock_file, init_val)
                            device.spi_slaves.append(slave)

                if "registers" in props:
                    reg_raw = props["registers"].replace("\\n", " ").replace('\\"', "").replace("\\t", " ")
                    reg_list = reg_raw.split(",")
                    for r_str in reg_list:
                        r_str = r_str.strip().strip('"').strip()
                        if "@" in r_str:
                            reg_direction = "RW"
                            if ":" in r_str:
                                r_str, dir_part = r_str.split(":", 1)
                                dir_part = dir_part.strip().strip("[").strip("]").upper()
                                if dir_part in ["RO", "WO", "RW"]:
                                    reg_direction = dir_part
                            elif "[" in r_str and "]" in r_str:
                                bracket_match = re.search(r"\[\s*(RO|WO|RW)\s*\]", r_str, re.IGNORECASE)
                                if bracket_match:
                                    reg_direction = bracket_match.group(1).upper()
                                    r_str = re.sub(r"\[\s*(RO|WO|RW)\s*\]", "", r_str, flags=re.IGNORECASE)

                            reg_parts = r_str.split("@")
                            reg_name = reg_parts[0].strip()
                            reg_offset = reg_parts[1].strip()

                            logical_name = None
                            paren_match = re.match(r"^([a-zA-Z0-9_]+)\s*\(\s*([a-zA-Z0-9_]+)\s*\)$", reg_name)
                            if paren_match:
                                reg_name = paren_match.group(1)
                                logical_name = paren_match.group(2)
                            direction_mode = None
                            l_upper = (logical_name or reg_name).upper()
                            r_upper = reg_name.upper()

                            is_dir_reg = (
                                "INV" in l_upper
                                or "TRI" in l_upper
                                or l_upper in ["DIR", "DDR"]
                                or r_upper in ["PDDR", "GDIR", "TRI", "DIR", "DDR"]
                            )
                            if r_upper.startswith("PDIR") or r_upper.startswith("PDOR"):
                                is_dir_reg = False

                            if is_dir_reg:
                                if "INV" in l_upper or "INV" in r_upper:
                                    direction_mode = "active_low_input"
                                else:
                                    direction_mode = "active_high_input"

                            device.registers.append(
                                Register(reg_name, reg_offset, reg_direction, logical_name, direction_mode)
                            )
                base_name = device.name
                counter = 1
                while any(d.name == device.name for d in devices):
                    device.name = f"{base_name}_{counter}"
                    counter += 1
                devices.append(device)

        # 共有メモリ名として使用するボード名を決定（UIO > GPIO > デフォルト）
        board_name = "vfpga_reg"
        uio = next((d for d in devices if d.type == "uio"), None)
        if uio:
            board_name = uio.name
        else:
            gpio = next((d for d in devices if d.type == "gpio"), None)
            if gpio:
                board_name = gpio.name

        model = BoardModel(devices, name=board_name)
        model.compatible_bytes = compatible_bytes
        model.model_name = model_name
        model.scenario_dir = os.path.dirname(dts_path)
        return model
