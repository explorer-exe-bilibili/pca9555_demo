#!/bin/bash
set +e
echo "===== 已绑定的 i2c 设备 ====="
for d in /sys/bus/i2c/devices/[0-9]*-*; do
  [ -e "$d" ] || continue
  printf '%-10s %s\n' "$(basename "$d")" "$(cat "$d/name" 2>/dev/null)"
done
echo
echo "===== 各适配器能力与地址占用 ====="
for b in 1 2 3 7; do
  echo "--- i2c-$b ($(cat /sys/class/i2c-adapter/i2c-$b/name 2>/dev/null)) ---"
  i2cdetect -y -r "$b" 2>&1 | sed -n '2,9p'
done
echo
echo "===== 0x40 / i2c-7 是什么 ====="
ls -d /sys/bus/i2c/devices/7-0040 2>&1
echo
echo "===== 40pin / 排针相关节点 ====="
ls /proc/device-tree/ | grep -iE "40pin|pin|hdr|io" | head -20
