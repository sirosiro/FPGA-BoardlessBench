#!/usr/bin/env python3
"""
@file scripts/tests/test_dts_parser.py
@intent:responsibility
    DTSParser および DTSParserError の正常系・異常系（セミコロン欠落、中括弧不整合、インクルード欠落）の単体テストを行う。
@intent:rationale
    初学者が DTS 記述でミスをした際に親切な診断レポートが確実に返されることを自動テストで恒久的に保証する。
@intent:pre-condition
    Python 3.8 以上であり、scripts/ ディレクトリがインポート可能であること。
"""

import os
import sys
import unittest
import tempfile

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from vfpga.parser import DTSParser, DTSParserError


class TestDTSParser(unittest.TestCase):

    def test_valid_scenario_parsing(self):
        """
        @intent:responsibility 既存の正常な DTS ファイルが例外なくパースできることを検証する。
        @intent:rationale 既存の 33 シナリオのパース互換性が破壊されていないことを担保する。
        """
        proj_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../"))
        dts_path = os.path.join(proj_root, "tests/scenarios/01_standard_uio/config.dts")
        model = DTSParser.parse(dts_path)
        self.assertIsNotNone(model)
        self.assertEqual(model.name, "vfpga_reg")
        self.assertEqual(len(model.devices), 1)

    def test_nonexistent_file_error(self):
        """
        @intent:responsibility 存在しないファイルを指定した場合に親切な DTSParserError が発生することを検証する。
        @intent:rationale ファイルパスのタイプミス時に適切なガイダンスを提供できるようにする。
        """
        with self.assertRaises(DTSParserError) as ctx:
            DTSParser.parse("/nonexistent/path/config.dts")
        err_msg = str(ctx.exception)
        self.assertIn("[DTSParser Error]", err_msg)
        self.assertIn("not found", err_msg)

    def test_missing_semicolon_error(self):
        """
        @intent:responsibility プロパティのセミコロン忘れで正確な行番号とスニペットが表示されることを検証する。
        @intent:rationale 初学者が最も遭遇しやすい文法ミスに対して的確な修正ヒントを提示する。
        """
        bad_dts = """/dts-v1/;
/ {
    vfpga_reg@40000000 {
        compatible = "generic-uio";
        label = "/dev/uio0"
        reg = <0x40000000 0x1000>;
    };
};
"""
        with tempfile.NamedTemporaryFile("w", suffix=".dts", delete=False) as f:
            f.write(bad_dts)
            f_path = f.name

        try:
            with self.assertRaises(DTSParserError) as ctx:
                DTSParser.parse(f_path)
            err_msg = str(ctx.exception)
            self.assertIn("[DTSParser Error]", err_msg)
            self.assertIn("Line     : 5", err_msg)
            self.assertIn("Missing terminating ';'", err_msg)
            self.assertIn(">    5 |         label = \"/dev/uio0\"", err_msg)
        finally:
            os.remove(f_path)

    def test_unclosed_brace_error(self):
        """
        @intent:responsibility 中括弧の閉じ忘れで正確なエラーが表示されることを検証する。
        @intent:rationale ノードネストの不整合を早期に特定できるようにする。
        """
        bad_dts = """/dts-v1/;
/ {
    vfpga_reg@40000000 {
        compatible = "generic-uio";
        label = "/dev/uio0";
        reg = <0x40000000 0x1000>;
};
"""
        with tempfile.NamedTemporaryFile("w", suffix=".dts", delete=False) as f:
            f.write(bad_dts)
            f_path = f.name

        try:
            with self.assertRaises(DTSParserError) as ctx:
                DTSParser.parse(f_path)
            err_msg = str(ctx.exception)
            self.assertIn("[DTSParser Error]", err_msg)
            self.assertIn("Unclosed opening brace", err_msg)
        finally:
            os.remove(f_path)

    def test_missing_include_error(self):
        """
        @intent:responsibility 存在しないインクルードファイルで正確なエラーが表示されることを検証する。
        @intent:rationale ヘッダー名間違いやパス指定ミスを即座に修正できるようにする。
        """
        bad_dts = """/dts-v1/;
#include "nonexistent_board_header.dtsi"
/ {
    model = "test";
};
"""
        with tempfile.NamedTemporaryFile("w", suffix=".dts", delete=False) as f:
            f.write(bad_dts)
            f_path = f.name

        try:
            with self.assertRaises(DTSParserError) as ctx:
                DTSParser.parse(f_path)
            err_msg = str(ctx.exception)
            self.assertIn("[DTSParser Error]", err_msg)
            self.assertIn("Included file not found", err_msg)
            self.assertIn("nonexistent_board_header.dtsi", err_msg)
        finally:
            os.remove(f_path)


    def test_gtr_include_and_label_merging(self):
        """
        @intent:responsibility GTR ファイルが DTS をインクルードし、&label でレジスタを正常にバインドできることを検証。
        """
        base_dts = """/dts-v1/;
/ {
    vfpga_reg: vfpga_reg@40000000 {
        compatible = "generic-uio";
        reg = <0x40000000 0x1000>;
        label = "/dev/uio0";
    };
};
"""
        with tempfile.TemporaryDirectory() as tmp_dir:
            dts_path = os.path.join(tmp_dir, "config.dts")
            gtr_path = os.path.join(tmp_dir, "config.gtr")
            with open(dts_path, "w") as f:
                f.write(base_dts)

            gtr_content = """/dts-v1/;
/include/ "config.dts";

&vfpga_reg {
    registers = "CTRL @ 0x00", "STATUS @ 0x04:RO";
};
"""
            with open(gtr_path, "w") as f:
                f.write(gtr_content)

            model = DTSParser.parse(gtr_path)
            self.assertEqual(len(model.devices), 1)
            self.assertEqual(len(model.devices[0].registers), 2)
            self.assertEqual(model.devices[0].registers[0].name, "CTRL")
            self.assertEqual(model.devices[0].registers[1].name, "STATUS")
            self.assertEqual(model.devices[0].registers[1].direction, "RO")

    def test_gtr_node_name_fallback_merging(self):
        """
        @intent:responsibility コロン付きラベルが無い場合でもノード名でフォールバック結合できることを検証。
        """
        base_dts = """/dts-v1/;
/ {
    vfpga_reg@40000000 {
        compatible = "generic-uio";
        reg = <0x40000000 0x1000>;
        label = "/dev/uio0";
    };
};
"""
        with tempfile.TemporaryDirectory() as tmp_dir:
            dts_path = os.path.join(tmp_dir, "config.dts")
            gtr_path = os.path.join(tmp_dir, "config.gtr")
            with open(dts_path, "w") as f:
                f.write(base_dts)

            gtr_content = """/dts-v1/;
#include "config.dts"

&vfpga_reg {
    registers = "RST @ 0x10";
};
"""
            with open(gtr_path, "w") as f:
                f.write(gtr_content)

            model = DTSParser.parse(gtr_path)
            self.assertEqual(len(model.devices[0].registers), 1)
            self.assertEqual(model.devices[0].registers[0].name, "RST")

    def test_unmerged_label_error(self):
        """
        @intent:responsibility 存在しないノードへの &label 参照時に無言破棄されず DTSParserError が発生することを検証。
        """
        base_dts = """/dts-v1/;
/ {
    vfpga_reg@40000000 {
        compatible = "generic-uio";
        reg = <0x40000000 0x1000>;
    };
};
"""
        with tempfile.TemporaryDirectory() as tmp_dir:
            dts_path = os.path.join(tmp_dir, "config.dts")
            gtr_path = os.path.join(tmp_dir, "config.gtr")
            with open(dts_path, "w") as f:
                f.write(base_dts)

            gtr_content = """/dts-v1/;
#include "config.dts"

&nonexistent_node {
    registers = "ERR @ 0x00";
};
"""
            with open(gtr_path, "w") as f:
                f.write(gtr_content)

            with self.assertRaises(DTSParserError) as ctx:
                DTSParser.parse(gtr_path)
            self.assertIn("not found in Device Tree", str(ctx.exception))
            self.assertIn("&nonexistent_node", str(ctx.exception))

    def test_quote_inside_block_comment(self):
        """
        @intent:responsibility ブロックコメント内の引用符（例: 0.56"）が後続コードの括弧を誤って巻き込まないことを検証。
        """
        dts = """/dts-v1/;
/* 0.56" 7-segment display */
/ {
    vfpga_reg: vfpga_reg@40000000 {
        compatible = "generic-uio";
        reg = <0x40000000 0x1000>;
    };
};
"""
        with tempfile.NamedTemporaryFile("w", suffix=".dts", delete=False) as f:
            f.write(dts)
            f_path = f.name
        try:
            model = DTSParser.parse(f_path)
            self.assertEqual(len(model.devices), 1)
            self.assertEqual(model.devices[0].name, "vfpga_reg")
        finally:
            os.remove(f_path)

    def test_commented_out_node_and_registers(self):
        """
        @intent:responsibility // や /* */ でコメントアウトされた &node や registers が実レジスタとして誤抽出されないことを検証。
        """
        gtr = """/dts-v1/;
/ {
    vfpga_reg: vfpga_reg@40000000 {
        compatible = "generic-uio";
        reg = <0x40000000 0x1000>;
    };
};

// &vfpga_reg {
//     registers = "UNUSED @ 0x00";
// };

/*
&vfpga_reg {
    registers = "BLOCK_UNUSED @ 0x04";
};
*/
"""
        with tempfile.NamedTemporaryFile("w", suffix=".gtr", delete=False) as f:
            f.write(gtr)
            f_path = f.name
        try:
            model = DTSParser.parse(f_path)
            self.assertEqual(len(model.devices), 1)
            self.assertEqual(len(model.devices[0].registers), 0)
        finally:
            os.remove(f_path)


if __name__ == "__main__":
    unittest.main()
