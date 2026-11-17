#!/bin/bash
# 板端验证：scan 耗时、空总线错误路径、--sim 全流程
set +e
B=/root/pca9555_demo/pca9555_tool
echo "===== scan 耗时 ====="
time $B scan >/dev/null 2>&1

echo
echo "===== 空总线 info（应给出友好错误） ====="
$B --bus 7 info; echo "rc=$?"

echo
echo "===== 只读好总线 + 错误地址 ====="
$B --bus 1 --addr 0x21 info; echo "rc=$?"

echo
echo "===== 总线不支持时（i2c-10 ddc） ====="
$B --bus 10 info; echo "rc=$?"

echo
echo "===== --sim info 表格对齐 ====="
$B --sim info

echo
echo "===== help ====="
$B --help | head -12
