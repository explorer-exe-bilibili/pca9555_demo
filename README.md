# PCA9555 I2C 扩展器 Demo（LubanCat-3IO / RK3576）

用 **CMake + C++17** 写的 PCA9555（16 位 I2C IO 扩展器）用户态驱动和命令行 demo：
控制输出、读取输入、监视引脚变化、非破坏性自检。
不依赖内核驱动、不依赖 `libi2c`，只用 `/dev/i2c-N` + `ioctl(I2C_RDWR)`，拷到板子上就能跑。

代码写在这台 Windows 机器上，用 **WSL 交叉编译成 aarch64** 再部署到板子。

---

## 1. 实测环境

| 项目 | 值 |
| --- | --- |
| 板卡 | EmbedFire **LubanCat-3IO**（RK3576，aarch64） |
| 系统 | Ubuntu 22.04.5 LTS，内核 `6.1.99-rk3576` |
| 登录 | `root@10.1.1.2`（密码用环境变量 `BOARD_PASS` 提供，不写进仓库） |
| 板端工具 | `i2c-tools 4.3`、`gcc/g++ 11.4`、`cmake 3.22`（本 demo 不需要板端编译） |
| 编译机 | WSL2 Ubuntu 24.04，`aarch64-linux-gnu-g++ 13.3`、`cmake 3.28`、`ninja` |

### 板子上的 I2C 现状（实测）

| 设备节点 | 控制器 | 已有设备 | 0x20-0x27 是否空闲 |
| --- | --- | --- | --- |
| `/dev/i2c-1` | rk3x-i2c | `0x23` rk806（PMIC，内核驱动占用） | 除 0x23 外空闲 |
| `/dev/i2c-2` | rk3x-i2c | `0x22` fusb302（USB-C，内核驱动占用） | 除 0x22 外空闲 |
| `/dev/i2c-3` | rk3x-i2c | `0x0c` dw9714、`0x11` es8388、`0x51` hym8563 | **空闲** |
| `/dev/i2c-7` | rk3x-i2c | `0x0c` dw9714，`0x40` 有应答但无驱动 | **空闲** |
| `/dev/i2c-10` | ddc | 显示 DDC 总线 | 不适用（探测会 EPERM） |
| `/dev/i2c-11` | dp | DisplayPort AUX | 不适用 |

结论：**PCA9555 用默认地址 `0x20`（A2/A1/A0 全接地）在 i2c-3 / i2c-7 上都不会冲突**。
这两条总线的引脚复用（设备树实测）：

```
i2c-3 -> i2c3m0-xfer : GPIO4_13(SCL) / GPIO4_12(SDA)
i2c-7 -> i2c7m1-xfer : GPIO3_0(SCL)  / GPIO3_1(SDA)
```

40pin 排针的物理 3 / 5 脚是 I2C（与 MIPI CAM3 共用、**已带 3.3V 上拉**，见
[野火 40pin 引脚说明](https://doc.embedfire.com/linux/rk3576/quick_start/zh/latest/doc/40pin/40pin.html)），
接线后跑一次 `pca9555_tool scan` 就能确认落在哪条 `/dev/i2c-N` 上，不用猜。

---

## 2. 目录结构

```
pca9555_demo/
├── CMakeLists.txt                  # 构建入口
├── cmake/toolchain-aarch64.cmake   # aarch64 交叉编译工具链文件
├── include/pca9555/
│   ├── errors.hpp                  # Error / I2cError
│   ├── transport.hpp               # 寄存器读写抽象（可替换后端）
│   ├── i2c_bus.hpp                 # /dev/i2c-N 实现（I2C_RDWR）
│   ├── logging_transport.hpp       # 事务日志装饰器（--verbose）
│   ├── sim_transport.hpp           # 内存模型（--sim / 单元测试）
│   └── pca9555.hpp                 # 驱动本体
├── src/                            # 上面各头文件的实现
├── apps/pca9555_tool.cpp           # 命令行 demo
├── tests/test_pca9555.cpp          # 单元测试（不需要硬件）
└── scripts/
    ├── build-wsl.sh                # WSL 一键构建（x86 测试 + aarch64 产物）
    ├── deploy-wsl.sh               # 部署到 10.1.1.2 并冒烟测试
    ├── full-check.sh               # 清空 build/ 后从零构建 + 部署验证
    ├── board_survey.sh             # 板端环境探查（只读）：总线、设备树、地址占用
    ├── board_map.sh                # 已绑定驱动与地址占用
    ├── board_pinmux.sh             # i2c 控制器 -> GPIO 复用解析
    ├── board_verify.sh             # 板端冒烟脚本
    ├── board_hw_check.sh           # 真实总线上验证 verbose 日志
    └── local_cli_check.sh          # 本机 --sim 跑一遍所有子命令
```

---

## 3. 编译

需要 WSL 里有交叉工具链：

```bash
sudo apt install -y g++-aarch64-linux-gnu cmake ninja-build
```

### 方式一：Windows 下双击/命令行执行

```bat
build.cmd
```

### 方式二：进 WSL 手动构建

```bash
cd /mnt/d/src/repo/.DIODE/pca9555_demo
bash scripts/build-wsl.sh
```

脚本做三件事：

1. `build/native`：x86_64 原生构建 + 跑单元测试（15 个用例 / 62 条断言，用内存模型，**不需要硬件**）；
2. `build/aarch64`：交叉编译出 `build/aarch64/pca9555_tool`；
3. 打印产物的架构信息。

产物是**静态链接**的 aarch64 ELF——因为构建机 glibc 是 2.39、板子是 2.35，静态链接过去不会有版本依赖问题。
想改成动态链接就加 `-DPCA9555_STATIC_LINK=OFF`。

手工交叉编译（不用脚本）：

```bash
cmake -S . -B build/aarch64 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
cmake --build build/aarch64 -j
```

---

## 4. 部署 / 运行

```bat
deploy.cmd                       :: 上传 + 板端冒烟测试
deploy.cmd --bus 7 info          :: 上传后直接执行命令
```

脚本用密码登录（不往板子的 `authorized_keys` 写东西）。**密码不写进仓库**，用环境变量或本地文件提供：

```bash
export BOARD_PASS='<板子密码>'          # 或者写进 ~/.pca9555_board_pass（chmod 600）
BOARD_HOST=10.1.1.2 BOARD_USER=root bash scripts/deploy-wsl.sh
```

手工部署：

```bash
wsl -d Ubuntu -- bash -c \
  "scp build/aarch64/pca9555_tool root@10.1.1.2:/root/pca9555_demo/"
ssh root@10.1.1.2 "chmod +x /root/pca9555_demo/pca9555_tool"
```

---

## 5. 命令速查

```bash
pca9555_tool [--bus N] [--addr 0xNN] [--sim] [-v] <命令> [参数]
```

| 命令 | 作用 |
| --- | --- |
| `scan` | 扫描所有 `/dev/i2c-*` 的 0x20-0x27，自动跳过被内核驱动占用的地址和不可用总线 |
| `info` | 打印 8 个寄存器和 16 个引脚的方向/电平/极性表 |
| `read` | 读输入端口，打印 16 位电平 |
| `write <hex>` | 写输出锁存（0x0000-0xFFFF） |
| `get <引脚>` | 读某引脚电平 |
| `set <引脚> <0\|1>` | 切成输出并写电平（先写锁存再切方向，无毛刺） |
| `toggle <引脚>` | 翻转输出引脚 |
| `dir <引脚> <in\|out>` | 设置方向 |
| `monitor [ms]` | 轮询输入，打印电平变化（Ctrl+C 退出） |
| `blink <引脚> [次数] [ms]` | 闪烁 |
| `selftest` | 非破坏性自检：先切全输入，再交叉写锁存并读回，最后恢复原配置 |
| `demo` | 完整演示：配置 → 走马灯 → 读输入 → 恢复 |

引脚写法：`0`-`15`（绝对编号）、`p03`、`p1.2`、`p1_2`、`P17`。

### 实际操作示例

```bash
# 1) 先看芯片挂在哪条总线、哪个地址
pca9555_tool scan

# 2) 看全部寄存器 + 引脚状态
pca9555_tool --bus 7 info

# 3) 把 P0.3 拉高，再读回来
pca9555_tool --bus 7 set p0.3 1
pca9555_tool --bus 7 get p0.3

# 4) 监视 P1 口 8 个按键输入
pca9555_tool --bus 7 monitor

# 5) 走马灯 10 次，每次 200ms
pca9555_tool --bus 7 blink p0.0 10 200

# 6) 硬件还没到？用内存模型看全部效果
pca9555_tool --sim demo
pca9555_tool --sim info

# 7) 排查 I2C 时序：打印每一次真实事务
pca9555_tool -v --bus 7 info
#   [i2c] R /dev/i2c-7@0x20 reg 0x00 -> FF FF FF FF 00 00 FF FF
```

`scan` 的输出示例（本板实测）：

```
扫描 6 条 I2C 总线上的地址区间 0x20-0x27：

/dev/i2c-1     [23:rk806]
/dev/i2c-2     [22:fusb302]
/dev/i2c-3
/dev/i2c-7
```

---

## 6. 接线（之后加装 PCA9555）

| PCA9555 引脚 | 接到 |
| --- | --- |
| VCC | 3.3V（注意模块上的电平，PCA9555 是 2.3-5.5V） |
| GND | GND |
| SDA | 板子 40pin 的 3 脚（SDA） |
| SCL | 板子 40pin 的 5 脚（SCL） |
| A0 / A1 / A2 | 全接地 → 地址 `0x20`；接 VCC 可改到 0x21-0x27 |
| ~INT | 可不接（本 demo 用轮询）；要接中断就引到某个 GPIO |
| P0.0-P0.7 / P1.0-P1.7 | 你的外设（LED、继电器、按键…） |

- 40pin 的 3/5 脚已有外部 3.3V 上拉，多数模块上的上拉可以不动；
- **别接 5V 上拉到 3.3V 的 IO 上**；
- 上电默认值：`CFG=0xFFFF`（全输入）、`OUT=0xFFFF`、`POL=0x0000`；
- 把某脚切成输出的瞬间，它会输出锁存里的值（复位是 1），所以 `set` 走的是"先写锁存、再切方向"的顺序，避免毛刺。

接好线后：

```bash
pca9555_tool scan                                   # 应看到 [20]
pca9555_tool --bus <scan 报的编号> --addr 0x20 info
pca9555_tool --bus <编号> --addr 0x20 selftest      # 寄存器回环确认芯片在线
```

---

## 7. 在自己的代码里用

```cpp
#include "pca9555/i2c_bus.hpp"
#include "pca9555/pca9555.hpp"

pca9555::I2cBus bus("/dev/i2c-7", 0x20);   // 打开总线并绑定从机地址
pca9555::Pca9555 io(bus);

io.configure(/*output=*/0x00FF,            // P0 全 0，P1 未用
             /*polarity=*/0x0000,
             /*direction=*/0xFF00);        // P0 输出、P1 输入（一次事务组写完）

io.writePin(3, true);                      // 等价 io.setPinOutput(3, true)
bool key = io.readPin(12);                 // 读 P1.4
std::uint16_t all = io.inputs();           // 一次读 16 位输入
```

所有错误都会抛异常：`pca9555::I2cError`（带 `errno`，从机不应答是 `ENXIO`/`EREMOTEIO`），
参数错误是 `std::invalid_argument` / `std::logic_error` / `std::out_of_range`。

---

## 8. 单元测试

```bash
bash scripts/build-wsl.sh                 # 会顺带跑
ctest --test-dir build/native --output-on-failure
./build/native/pca9555_tests              # 单独跑，看每个用例
```

测试用 `SimTransport`（内存里模拟一颗 PCA9555）覆盖：上电复位值、方向/极性/输出锁存的读写、
单引脚操作、极性反转对 IN 的影响、输入/输出引脚的电平语义、事务合并次数、
引脚名解析、越界与故障注入。

---

## 9. 排查

| 现象 | 原因 / 处理 |
| --- | --- |
| `I2C 错误：... errno=6 (ENXIO)` | 该地址没有从机应答：检查供电、SDA/SCL 是否接反、上拉、A0-A2 地址 |
| `scan` 里 `[20]` 不出现 | 芯片没上电 / 总线选错 / 该总线在设备树里没使能 |
| `Permission denied` | 需要 root（`/dev/i2c-*` 属 `root:i2c`，普通用户要 `usermod -aG i2c $USER`） |
| `Operation not permitted (EPERM)` | 这条总线不支持普通 I2C（`i2c-10` DDC / `i2c-11` DP），换总线 |
| 地址显示 `[23:rk806]` 这类 | 该地址已被内核驱动绑定，不是你的 PCA9555，忽略 |
| 读到一直是 `0xFFFF` | 引脚都是输入且外部悬空/上拉；先 `dir p0.0 out` 再写 |
| 写输出没反应 | 该脚还是输入（`info` 看方向），或外部电路把它拉住了 |

排查时序问题时加 `-v`，会打印每一次真实的 I2C 事务（地址、寄存器、数据）。

---

## 10. 设计要点

- **分层**：`IRegisterTransport`（寄存器读写抽象）→ `Pca9555`（器件语义）。
  硬件后端 `I2cBus`、内存后端 `SimTransport`、日志装饰器 `LoggingTransport` 都实现同一接口，
  所以驱动逻辑能在 x86 上完整测试，不用等硬件。
- **为什么用 `I2C_RDWR`**：PCA9555 是"寄存器指针 + 自动递增连续读写"的器件。
  `I2C_RDWR` 能把读/写合进一次事务，且不依赖 `libi2c` 的 SMBus 支持（`i2c-dev.h` 里
  SMBus 辅助函数需要 `libi2c-dev`，部分适配器也不支持）。
- **一次读 8 字节**：`info`/`refresh` 用单次事务读 `0x00-0x07`，既快又保证快照一致。
- **影子寄存器**：驱动在内存里维护方向/极性/输出锁存三份副本，单引脚操作走读-改-写，
  每次只发一次写事务。
- **无毛刺切换**：切成输出前先写锁存（`setPinOutput`），避免旧锁存值把引脚顶一下。
- **线程安全**：`I2cBus` 内部有互斥锁，多个线程共用一个对象不会交错事务。
- **静态链接**：构建机 glibc 2.39 > 板子 2.35，静态链接避免 `GLIBC_2.xx not found`。

## 11. 后续可以怎么改

- 需要中断唤醒：把 `~INT` 接到 GPIO，用 `poll()` 等边沿，再调 `inputs()` 读全 16 位；
- 需要和内核驱动共存：在设备树里加 `pca9555@20 { compatible = "nxp,pca9555"; reg = <0x20>; }`，
  之后走 `/sys/class/gpio` 或 `libgpiod`，本工具会提示该地址已被驱动占用；
- 多片 PCA9555：`--addr 0x20..0x27` 分别实例化多个 `Pca9555` 对象即可。
