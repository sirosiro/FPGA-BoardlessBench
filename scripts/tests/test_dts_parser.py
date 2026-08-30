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


if __name__ == "__main__":
    unittest.main()
