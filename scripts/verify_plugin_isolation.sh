#!/bin/bash
# ==============================================================================
# verify_plugin_isolation.sh
# FPGA-BoardlessBench (F-BB) プラグイン完全隔離・具象名漏洩検知スクリプト
#
# ARCHITECTURE_MANIFEST.md Section 3.12 準拠:
# F-BB コア（dashboard/, start_lab.sh, src/controller/, scripts/ 等）に
# サードパーティ・プラグイン固有の具象名が混入していないかを静的検証する。
# ==============================================================================
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

echo "=========================================================="
echo "  [Linter] Verifying Plugin Isolation in F-BB Core..."
echo "=========================================================="

# 検出対象の禁止プラグインキーワード（正規表現）
# サードパーティ固有のバイナリ名、デーモン名、スクリプト名、固有SHM名
FORBIDDEN_REGEX="(space_guidance|pilot\.py|rct_z80|retro_core_tracer|fbb_space_guidance_shm)"

# 検査対象ファイルおよびディレクトリ（F-BB コアコンポーネント）
INSPECT_TARGETS=(
  "$REPO_ROOT/start_lab.sh"
  "$REPO_ROOT/dashboard/server.js"
  "$REPO_ROOT/src/controller/vlogic_controller.py"
  "$REPO_ROOT/scripts/vfpga"
)

VIOLATIONS=0

for target in "${INSPECT_TARGETS[@]}"; do
  if [ -e "$target" ]; then
    # 対象ファイル内の違反を検索（コメント行の汎用言及は除外せず厳格チェック）
    MATCHES=$(grep -rnE "$FORBIDDEN_REGEX" "$target" 2>/dev/null || true)
    if [ -n "$MATCHES" ]; then
      echo "❌ VIOLATION FOUND in: $target"
      echo "$MATCHES" | while read -r line; do
        echo "   -> $line"
      done
      echo ""
      VIOLATIONS=$((VIOLATIONS + 1))
    fi
  fi
done

if [ "$VIOLATIONS" -gt 0 ]; then
  echo "=========================================================="
  echo "🚨 FAILED: Found $VIOLATIONS plugin isolation violation(s)!"
  echo "   F-BB core host must NEVER hardcode plugin-specific"
  echo "   binary, daemon, script, or SHM identifiers."
  echo "   See ARCHITECTURE_MANIFEST.md Section 3.12."
  echo "=========================================================="
  exit 1
fi

echo "✨ 100% CLEAN: Zero plugin-specific identifiers in F-BB Core."
echo "=========================================================="
exit 0
