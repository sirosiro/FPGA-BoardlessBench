# Scenario 01c_protocol_assertion

## 1. 概要 (Overview)
F-BB 次世代機能 **トランザクション・ロガー＆プロトコル・アサーション・エンジン (Transaction Logger & Protocol Assertion Engine)** の動作検証用シナリオです。

## 2. 検証内容
`config.dts` 内で `STATUS @ 0x04 : RO` (Read-Only) として定義されたレジスタに対し、FW から誤った Write 操作 (`vfpga[1] = 0xDEADBEEF`) を試行します。
C-Shim (`libfpgashim.so`) がアクセス権限違反を検知し、標準エラー出力へのアサート警告と `/tmp/fbb_protocol_violations.log` へのログ記録が正しく行われることを自動アサートします。

## 3. 実行方法
```bash
fbb test 01c_protocol_assertion
```
