#!/bin/bash
# 在 WSL 里一键构建本工程：
#   1) x86_64 原生构建 + 单元测试（用内存模型，不需要硬件）
#   2) aarch64 交叉编译（给 RK3576 板子用）
# 用法：bash scripts/build-wsl.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

JOBS="$(nproc)"
GEN=()
if command -v ninja >/dev/null 2>&1; then
    GEN=(-G Ninja)
fi

echo "=============================================================="
echo " 工程目录 : $ROOT"
echo " 并行任务 : $JOBS"
echo "=============================================================="

# ---------------------------------------------------------------- 1. 原生测试
echo
echo "[1/2] 原生构建 + 单元测试 (x86_64)"
cmake -S . -B build/native "${GEN[@]}" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DPCA9555_BUILD_TESTS=ON \
      -DPCA9555_STATIC_LINK=OFF >/dev/null
cmake --build build/native -j "$JOBS"
ctest --test-dir build/native --output-on-failure

# ---------------------------------------------------------------- 2. 交叉编译
echo
echo "[2/2] 交叉编译 aarch64 (RK3576)"
if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    echo "缺少交叉工具链，请先安装：sudo apt install g++-aarch64-linux-gnu" >&2
    exit 1
fi
cmake -S . -B build/aarch64 "${GEN[@]}" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DPCA9555_BUILD_TESTS=OFF >/dev/null
cmake --build build/aarch64 -j "$JOBS"

echo
echo "=============================================================="
echo " 产物："
ls -l build/aarch64/pca9555_tool
file build/aarch64/pca9555_tool | sed 's/, BuildID[^,]*//'
echo
echo " 下一步：bash scripts/deploy-wsl.sh"
echo "=============================================================="
