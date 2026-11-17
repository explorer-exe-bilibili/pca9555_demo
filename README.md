# PCA9555 驱动库使用指南

## 1. 概述

`PCA9555.hpp` / `PCA9555.cpp` 是一个面向 Linux I2C 子系统的 **PCA9555 16 路 IO 扩展器驱动库**。
纯用户态实现，只用 `/dev/i2c-N`，不依赖内核驱动，也不需要额外库。

## 2. 硬件连接

```
  PCA9555            主控 (LubanCat-3IO / ARM 板)
 ┌──────────┐       ┌──────────────────┐
 │ SDA  ────┼───────┤ 40pin 第 3 脚     │
 │ SCL  ────┼───────┤ 40pin 第 5 脚     │
 │ VCC  ────┼───────┤ 3.3V             │
 │ GND  ────┼───────┤ GND              │
 │ A0~A2 ───┼───────┤ GND (地址 = 0x20) │
 └──────────┘       └──────────────────┘
```

> 40pin 的 3/5 脚自带 3.3V 上拉，模块上的上拉可以不动。A0~A2 接 VCC 可改成 0x21~0x27。

## 3. 快速开始

### 3.1 编译

代码在本机写，用 WSL 交叉编译给板子（板子是 aarch64）：

```bash
wsl -d Ubuntu -- bash -c '
  cd /mnt/d/src/repo/.DIODE/pca9555_demo &&
  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64.cmake &&
  cmake --build build'
```

`cmake/aarch64.cmake` 里额外加了 `-static`：构建机 glibc 比板子新，静态链接拷过去直接跑。

> 直接在板子上编译也可以：`g++ -O2 main.cpp PCA9555.cpp -o pca9555_demo`

### 3.2 最简示例

```cpp
#include "PCA9555.hpp"

int main() {
    PCA9555 io("/dev/i2c-7", 0x20);   // 1. 打开设备
    if (!io.begin()) return 1;        // 2. 复位 + 确认在线

    io.setOutput(0x00FF);             // 3. 先写输出锁存
    io.setDirection(0xFF00);          // 4. P0 输出, P1 输入

    io.setPin(3, false);              // 5. P0.3 拉低 (点亮 LED)
    uint16_t in = io.input();         // 6. 读 16 路输入
    bool key = io.getPin(12);         //    读 P1.4

    io.setDirection(0xFFFF);          // 7. 退出前恢复全输入
    return 0;
}
```

## 4. API 参考

### 4.1 构造

```cpp
PCA9555 io("/dev/i2c-7");        // 默认地址 0x20
PCA9555 io("/dev/i2c-7", 0x21);  // 指定地址

bool ok = io.isReady();          // I2C 是否成功打开
```

### 4.2 初始化

```cpp
bool ok = io.begin();   // 读寄存器确认在线, 然后恢复上电默认 (全输入/锁存全 1/不反转)
io.reset();             // 单独调用也可以
```

### 4.3 方向

```cpp
io.setDirection(0xFF00);        // P0 输出, P1 输入 (1 = 输入)
uint16_t dir = io.direction();
```

> 切成输出前请先 `setOutput()` 写好锁存，否则引脚会立刻输出锁存里的旧值。

### 4.4 输出 / 输入

```cpp
io.setOutput(0x00FF);           // 写 16 位输出锁存
uint16_t out = io.output();     // 读锁存
uint16_t in  = io.input();      // 读 16 路引脚电平
```

### 4.5 极性反转

```cpp
io.setPolarity(0x000F);         // P0.0~P0.3 读回取反
uint16_t pol = io.polarity();
```

### 4.6 单引脚

```cpp
io.setPin(3, false);            // 拉低 (读回锁存, 只改这一位)
bool level = io.getPin(3);
```

### 4.7 调试

```cpp
io.dumpRegisters();             // 打印 IN / OUT / POL / CFG 到 stdout
```

## 5. 引脚与寄存器

| 地址 | 名称 | 说明 |
| --- | --- | --- |
| 0x00 / 0x01 | IN0 / IN1 | 输入电平，只读 |
| 0x02 / 0x03 | OUT0 / OUT1 | 输出锁存 |
| 0x04 / 0x05 | POL0 / POL1 | 极性反转，1 = 读回取反 |
| 0x06 / 0x07 | CFG0 / CFG1 | 方向，1 = 输入，0 = 输出 |

- 低字节在前：写 0x02 两个字节就是同时写 OUT0 和 OUT1；
- 上电默认 `CFG=0xFFFF`、`OUT=0xFFFF`、`POL=0x0000`；
- 引脚编号：`P0.0~P0.7` 对应 bit0~bit7，`P1.0~P1.7` 对应 bit8~bit15。

## 6. 常见问题

| 现象 | 可能原因 | 解决 |
| --- | --- | --- |
| `地址 0x20 无应答` | 接线 / 供电 / 地址 | `i2cdetect -y 7` 先确认地址 |
| `打开 /dev/i2c-7 失败` | 总线不存在或无权限 | 换总线号；用 root 或加入 `i2c` 组 |
| 写输出没有反应 | 该引脚还是输入 | `setDirection()` 把它配成输出 |
| 读到一直是 0xFFFF | 输入悬空被上拉 | 外部下拉或按键接地 |
| `i2c-10`/`i2c-11` 报 EPERM | 那是显示 DDC/AUX 总线 | 换 `i2c-1/2/3/7` |
