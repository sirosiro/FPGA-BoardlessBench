#!/bin/bash
# FPGA-BoardlessBench (F-BB) Dashboard Rebuild Script
# このスクリプトは、フロントエンドをリビルドして最新の変更をバックエンドに反映します。

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

echo "===================================================="
echo "   Rebuilding F-BB Diagnostic Dashboard..."
echo "===================================================="

# 1. バックエンド依存関係の確認・インストール
echo "[1/3] Checking backend dependencies..."
cd "$SCRIPT_DIR"
if [ ! -d "node_modules" ]; then
    echo "Installing backend dependencies..."
    npm install
else
    echo "Backend dependencies are already installed."
fi

# 2. フロントエンド依存関係の確認・インストール
echo "[2/3] Checking frontend dependencies..."
cd "$SCRIPT_DIR/client"
if [ ! -d "node_modules" ]; then
    echo "Installing frontend dependencies (this may take a moment)..."
    npm install
else
    echo "Frontend dependencies are already installed."
fi

# 3. フロントエンドのビルド
echo "[3/3] Building frontend assets with Vite..."
npm run build

if [ $? -eq 0 ]; then
    echo "===================================================="
    echo "   Dashboard rebuilt successfully!"
    echo "===================================================="
else
    echo "===================================================="
    echo "   Error: Dashboard rebuild failed!"
    echo "   Check the log output above for details."
    echo "===================================================="
    exit 1
fi
