#!/bin/bash
# 板端只读环境探查：系统、I2C 总线、设备树、适配器能力、驱动占用。
# 用法： ssh root@10.1.1.2 'bash -s' < scripts/board_survey.sh
set +e

echo "========== 1. 系统 =========="
tr -d '\0' < /proc/device-tree/model; echo
head -2 /etc/os-release
uname -a

echo
echo "========== 2. I2C 总线 =========="
ls -l /dev/i2c-* 2>&1
i2cdetect -l 2>&1

echo
echo "========== 3. 已绑定驱动 =========="
for d in /sys/bus/i2c/devices/[0-9]*-*; do
  [ -e "$d" ] || continue
  printf '%-10s %s\n' "$(basename "$d")" "$(cat "$d/name" 2>/dev/null)"
done

echo
echo "========== 4. 设备树里的 i2c 控制器 =========="
for d in /proc/device-tree/i2c@*; do
  st=$(tr -d '\0' < "$d/status" 2>/dev/null)
  [ "$st" = "okay" ] || continue
  printf '%-22s ' "${d##*/}"
  for c in "$d"/*@*; do
    [ -d "$c" ] || continue
    printf '%s ' "${c##*/}"
  done
  echo
done

echo
echo "========== 5. 适配器能力与地址占用 =========="
for b in /dev/i2c-*; do
  [ -e "$b" ] || continue
  n=${b#/dev/i2c-}
  echo "--- i2c-$n ($(cat /sys/class/i2c-adapter/i2c-$n/name 2>/dev/null)) ---"
  timeout 5 i2cdetect -F "$n" 2>&1 | grep -iE 'I2C_FUNC_I2C|I2C_FUNC_SMBUS_(READ|WRITE)_BYTE' | head -3
  timeout 8 i2cdetect -y -r "$n" 2>&1 | sed -n '2,9p'
done

echo
echo "========== 6. 板端编译环境（仅作参考，不需要） =========="
gcc --version 2>/dev/null | head -1
cmake --version 2>/dev/null | head -1
