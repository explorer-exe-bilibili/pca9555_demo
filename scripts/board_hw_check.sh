#!/bin/bash
# 板端硬件路径验证：真实总线上的 verbose 事务日志 + scan 输出格式
set +e
B=/root/pca9555_demo/pca9555_tool

echo "===== scan（格式） ====="
$B scan

echo
echo "===== 真实总线上的 verbose 事务日志（i2c-7 上 0x40 有应答设备，只读 2 字节） ====="
$B -v --bus 7 --addr 0x40 read
echo "rc=$?"

echo
echo "===== 空地址的错误提示 ====="
$B --bus 7 info
echo "rc=$?"
