#!/bin/bash
# 从零开始的完整验证：清空 build/ -> build.cmd 同款流程 -> 部署 -> 板端跑一遍
set -e
cd /mnt/d/src/repo/.DIODE/pca9555_demo

echo "############ 1. 清理并重新构建 ############"
rm -rf build
bash scripts/build-wsl.sh

echo
echo "############ 2. 部署并冒烟 ############"
bash scripts/deploy-wsl.sh
