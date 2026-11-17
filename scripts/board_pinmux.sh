#!/bin/bash
# 把 i2c 控制器绑定的 pinmux 解析成 i2cNmX（N=控制器，mX=复用组）
set +e

# phandle -> pinmux 名字
declare -A PH2NAME
for n in /proc/device-tree/pinctrl*/i2c*/i2c*-xfer; do
  [ -e "$n/phandle" ] || continue
  ph=$(xxd -p "$n/phandle" | tr -d '\n')
  PH2NAME["$ph"]="${n##*/}"
done

echo "===== 生效中的 i2c 控制器 ====="
for d in /proc/device-tree/i2c@*; do
  st=$(tr -d '\0' < "$d/status" 2>/dev/null)
  [ "$st" = "okay" ] || continue
  ph=$(xxd -p "$d/pinctrl-0" 2>/dev/null | tr -d '\n')
  addr=$(xxd -p "$d/reg" | tr -d '\n' | cut -c25-32)
  echo "${d##*/}  reg=0x$addr  pinmux=${PH2NAME[$ph]:-?($ph)}"
done

echo
echo "===== 各 pinmux 对应的 GPIO ====="
for m in /proc/device-tree/pinctrl*/i2c*/i2c*-xfer; do
  nm="${m##*/}"
  pins=$(ls "$m" | grep -E '^rockchip,pins$' >/dev/null && echo)
  if [ -e "$m/rockchip,pins" ]; then
    # 每 4 个 32 位整数一组：bank, pin, fn, phandle
    od -An -tu4 --endian=big "$m/rockchip,pins" | tr -s ' ' '\n' | grep -v '^$' | \
    awk -v name="$nm" 'BEGIN{i=0;buf[0]="";buf[1]="";buf[2]="";buf[3]=""}
      {buf[i%4]=$1; i++;
       if(i%4==0) printf "%-16s GPIO%d_%s  fn=%s\n", name, buf[0], buf[1], buf[2]}'
  fi
done | sort | head -30
