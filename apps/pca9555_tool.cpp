// SPDX-License-Identifier: MIT
/// @file pca9555_tool.cpp
/// @brief PCA9555 命令行演示程序：扫描、读引脚、写引脚、监视、走马灯、自检。
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <chrono>
#include <csignal>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "pca9555/errors.hpp"
#include "pca9555/i2c_bus.hpp"
#include "pca9555/logging_transport.hpp"
#include "pca9555/pca9555.hpp"
#include "pca9555/sim_transport.hpp"

namespace {

using pca9555::I2cBus;
using pca9555::IRegisterTransport;
using pca9555::LoggingTransport;
using pca9555::Pca9555;
using pca9555::SimTransport;

volatile std::sig_atomic_t g_stop = 0;

extern "C" void handleSignal(int) { g_stop = 1; }

struct Options {
    std::string bus;                                  // "7" / "i2c-7" / "/dev/i2c-7"
    std::uint16_t addr = Pca9555::kBaseAddress;       // 0x20
    bool sim = false;
    bool verbose = false;
};

// ---------------------------------------------------------------- 小工具

/// 支持十进制与 0x 前缀的整数解析。
unsigned parseNumber(const std::string& text, const char* what) {
    if (text.empty()) {
        throw std::invalid_argument(std::string(what) + " 不能为空");
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(text.c_str(), &end, 0);
    if (errno != 0 || end == nullptr || *end != '\0') {
        throw std::invalid_argument(std::string("无法解析") + what + " \"" + text + "\"");
    }
    return static_cast<unsigned>(value);
}

std::string toHex(unsigned value, int width) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%0*X", width, value);
    return buf;
}

std::string binary16(std::uint16_t value) {
    std::string s;
    s.reserve(20);
    for (int i = 15; i >= 0; --i) {
        s.push_back(((value >> i) & 1u) ? '1' : '0');
        if (i == 8) {
            s.push_back('\'');
        }
    }
    return s;
}

std::string normalizeDevice(const std::string& bus) {
    if (bus.empty()) {
        return bus;
    }
    if (bus.rfind("/dev/", 0) == 0) {
        return bus;
    }
    if (bus.rfind("i2c-", 0) == 0) {
        return "/dev/" + bus;
    }
    return "/dev/i2c-" + bus;
}

bool parseLevel(const std::string& text) {
    if (text == "1" || text == "on" || text == "high" || text == "h") {
        return true;
    }
    if (text == "0" || text == "off" || text == "low" || text == "l") {
        return false;
    }
    throw std::invalid_argument("电平只接受 0/1（也接受 on/off、high/low）");
}

bool parseInputFlag(const std::string& text) {
    if (text == "in" || text == "input" || text == "i") {
        return true;
    }
    if (text == "out" || text == "output" || text == "o") {
        return false;
    }
    throw std::invalid_argument("方向只接受 in 或 out");
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm_buf.tm_hour, tm_buf.tm_min,
                  tm_buf.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

/// 终端显示宽度：ASCII 记 1，CJK 等宽字符记 2（printf 的 %-Ns 按字节算，中文会错位）。
std::size_t displayWidth(const std::string& text) {
    std::size_t width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned codepoint = c;
        std::size_t length = 1;
        if (c >= 0xF0) {
            length = 4;
        } else if (c >= 0xE0) {
            length = 3;
        } else if (c >= 0xC0) {
            length = 2;
        }
        if (length > 1 && i + length <= text.size()) {
            codepoint = c & (0xFFu >> (length + 1));
            for (std::size_t k = 1; k < length; ++k) {
                codepoint = (codepoint << 6) |
                            (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
            }
        }
        const bool wide = (codepoint >= 0x1100 && codepoint <= 0x115F) ||
                          (codepoint >= 0x2E80 && codepoint <= 0xA4CF) ||
                          (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
                          (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
                          (codepoint >= 0xFE30 && codepoint <= 0xFE4F) ||
                          (codepoint >= 0xFF00 && codepoint <= 0xFF60) ||
                          (codepoint >= 0xFFE0 && codepoint <= 0xFFE6);
        width += wide ? 2 : 1;
        i += length;
    }
    return width;
}

std::string padRight(const std::string& text, std::size_t width) {
    const std::size_t current = displayWidth(text);
    return current >= width ? text : text + std::string(width - current, ' ');
}

std::unique_ptr<IRegisterTransport> makeTransport(const Options& opt) {
    std::unique_ptr<IRegisterTransport> transport;
    if (opt.sim) {
        transport = std::make_unique<SimTransport>();
    } else if (opt.bus.empty()) {
        throw pca9555::Error(
            "未指定 I2C 总线。请加 --bus <编号>（如 --bus 7），"
            "或先用 scan 子命令查看哪条总线上有 0x20-0x27 的设备；"
            "没有硬件时可加 --sim 用内存模型演示。");
    } else {
        const std::string device = normalizeDevice(opt.bus);
        const std::string driver = I2cBus::boundDriver(device, opt.addr);
        if (!driver.empty()) {
            std::printf("注意：%s 的地址 0x%02X 已被内核驱动 %s 占用，"
                        "读写可能与驱动冲突。\n",
                        device.c_str(), opt.addr, driver.c_str());
        }
        transport = std::make_unique<I2cBus>(device, opt.addr);
    }
    if (opt.verbose) {
        transport = std::make_unique<LoggingTransport>(std::move(transport));
    }
    return transport;
}

void printPinTable(std::uint16_t inputs, std::uint16_t outputs, std::uint16_t config,
                   std::uint16_t polarity) {
    const std::string header_a = padRight("引脚", 7) + padRight("方向", 6) +
                                 padRight("电平", 6) + padRight("极性", 6);
    std::printf("  %s  %s\n", header_a.c_str(), header_a.c_str());
    for (unsigned i = 0; i < 8; ++i) {
        std::string line;
        for (unsigned port = 0; port < 2; ++port) {
            const unsigned pin = port * 8 + i;
            const std::string dir = (config >> pin) & 1u ? "IN" : "OUT";
            const std::string lvl = (inputs >> pin) & 1u ? "1" : "0";
            const std::string inv = (polarity >> pin) & 1u ? "inv" : "-";
            line += padRight(Pca9555::formatPin(pin), 7) + padRight(dir, 6) +
                    padRight(lvl, 6) + padRight(inv, 6);
            if (port == 0) {
                line += "  ";
            }
        }
        std::printf("  %s\n", line.c_str());
    }
    std::printf("  (输出锁存 P0=%s P1=%s)\n", toHex(outputs & 0xFF, 2).c_str(),
                toHex((outputs >> 8) & 0xFF, 2).c_str());
}

/// 一次读回 0x00-0x07 全部寄存器。
struct RegisterSnapshot {
    std::uint16_t inputs{0};
    std::uint16_t outputs{0};
    std::uint16_t polarity{0};
    std::uint16_t config{0};
};

RegisterSnapshot readAll(IRegisterTransport& transport) {
    std::uint8_t regs[8] = {0};
    transport.readRegisters(Pca9555::kRegInput0, regs, sizeof regs);
    RegisterSnapshot s;
    s.inputs = static_cast<std::uint16_t>(regs[0] | (regs[1] << 8));
    s.outputs = static_cast<std::uint16_t>(regs[2] | (regs[3] << 8));
    s.polarity = static_cast<std::uint16_t>(regs[4] | (regs[5] << 8));
    s.config = static_cast<std::uint16_t>(regs[6] | (regs[7] << 8));
    return s;
}

void sleepMs(unsigned ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------- 子命令

int cmdScan(const Options&) {
    const std::vector<std::string> devices = I2cBus::listDevices();
    if (devices.empty()) {
        std::printf("没有找到 /dev/i2c-* 设备节点。\n");
        return 1;
    }
    std::printf("扫描 %zu 条 I2C 总线上的地址区间 0x%02X-0x%02X：\n\n", devices.size(),
                Pca9555::kBaseAddress, Pca9555::kLastAddress);

    unsigned free_hits = 0;
    unsigned bound_hits = 0;
    for (const std::string& dev : devices) {
        std::printf("%-14s", dev.c_str());

        bool usable = true;
        try {
            usable = I2cBus::supportsI2cTransfer(dev);
        } catch (const pca9555::I2cError& e) {
            std::printf(" 跳过：%s\n", e.what());
            continue;
        }
        if (!usable) {
            std::printf(" 跳过（适配器不支持 I2C_FUNC_I2C，无法做连续寄存器读写）\n");
            continue;
        }

        for (unsigned addr = Pca9555::kBaseAddress; addr <= Pca9555::kLastAddress; ++addr) {
            const auto a = static_cast<std::uint16_t>(addr);
            const std::string driver = I2cBus::boundDriver(dev, a);
            if (!driver.empty()) {
                std::printf(" [%02X:%s]", addr, driver.c_str());
                ++bound_hits;
                continue;
            }
            bool present = false;
            try {
                present = I2cBus::probe(dev, a);
            } catch (const pca9555::I2cError& e) {
                std::printf(" 跳过（%s）\n",
                            e.code() > 0 ? std::strerror(e.code()) : e.what());
                break;
            }
            if (present) {
                std::printf(" [%02X]", addr);
                ++free_hits;
            }
        }
        std::printf("\n");
    }

    std::printf("\n");
    if (free_hits > 0) {
        std::printf("发现 %u 个未被内核驱动占用的应答地址（候选 PCA9555）。\n", free_hits);
        std::printf("下一步：\n");
        std::printf("  pca9555_tool --bus <总线号> --addr <地址> info      # 看寄存器\n");
        std::printf("  pca9555_tool --bus <总线号> --addr <地址> selftest  # 寄存器回环确认\n");
    }
    if (bound_hits > 0) {
        std::printf("另有 %u 个地址已被内核驱动占用（打印为 [地址:驱动名]），"
                    "它们不是给 PCA9555 用的，可以忽略。\n",
                    bound_hits);
    }
    if (free_hits == 0) {
        std::printf(
            "没有找到空闲的 PCA9555 应答。请确认：\n"
            "  1) 芯片已上电，SDA/SCL 接在上面某条总线上；\n"
            "  2) A0/A1/A2 决定地址（全接地 = 0x20）；\n"
            "  3) SDA/SCL 上拉电阻已装（通常 4.7k）；\n"
            "  4) 若该总线在设备树里被其他功能复用，需要先确认引脚分配。\n");
        return 1;
    }
    return 0;
}

int cmdInfo(const Options& opt) {
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    const RegisterSnapshot s = readAll(*transport);
    std::printf("设备   : %s\n", transport->describe().c_str());
    std::printf("寄存器 :\n");
    std::printf("  IN0  %-6s IN1  %-6s\n", toHex(s.inputs & 0xFF, 2).c_str(),
                toHex((s.inputs >> 8) & 0xFF, 2).c_str());
    std::printf("  OUT0 %-6s OUT1 %-6s\n", toHex(s.outputs & 0xFF, 2).c_str(),
                toHex((s.outputs >> 8) & 0xFF, 2).c_str());
    std::printf("  POL0 %-6s POL1 %-6s\n", toHex(s.polarity & 0xFF, 2).c_str(),
                toHex((s.polarity >> 8) & 0xFF, 2).c_str());
    std::printf("  CFG0 %-6s CFG1 %-6s\n", toHex(s.config & 0xFF, 2).c_str(),
                toHex((s.config >> 8) & 0xFF, 2).c_str());
    std::printf("输入   : 0x%04X  %s\n", s.inputs, binary16(s.inputs).c_str());
    std::printf("引脚   :\n");
    printPinTable(s.inputs, s.outputs, s.config, s.polarity);
    return 0;
}

int cmdRead(const Options& opt) {
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    const std::uint16_t value = dev.inputs();
    std::printf("%s : 0x%04X  %s\n", transport->describe().c_str(), value,
                binary16(value).c_str());
    return 0;
}

int cmdWrite(const Options& opt, const std::vector<std::string>& args) {
    if (args.empty()) {
        throw std::invalid_argument("用法：write <0x0000-0xFFFF>");
    }
    const unsigned value = parseNumber(args[0], "输出值");
    if (value > 0xFFFF) {
        throw std::invalid_argument("输出值必须在 0x0000-0xFFFF 之间");
    }
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    dev.setOutputs(static_cast<std::uint16_t>(value));
    const std::uint16_t readback = dev.outputs();
    std::printf("已写输出锁存 = 0x%04X，读回 = 0x%04X %s\n", value, readback,
                readback == value ? "(一致)" : "(不一致！)");
    return readback == value ? 0 : 1;
}

int cmdGet(const Options& opt, const std::vector<std::string>& args) {
    if (args.empty()) {
        throw std::invalid_argument("用法：get <引脚>，例如 get p03 / get 3 / get p1.2");
    }
    const unsigned pin = Pca9555::parsePin(args[0]);
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    const bool level = dev.readPin(pin);
    std::printf("%s = %d (%s)\n", Pca9555::formatPin(pin).c_str(), level ? 1 : 0,
                level ? "高" : "低");
    return 0;
}

int cmdSet(const Options& opt, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        throw std::invalid_argument("用法：set <引脚> <0|1>");
    }
    const unsigned pin = Pca9555::parsePin(args[0]);
    const bool level = parseLevel(args[1]);
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    dev.setPinOutput(pin, level);  // 先写锁存再切方向，无毛刺
    const bool readback = dev.readPin(pin);
    std::printf("%s 设为输出 = %d，读回 %d %s\n", Pca9555::formatPin(pin).c_str(),
                level ? 1 : 0, readback ? 1 : 0, readback == level ? "(一致)" : "(不一致！)");
    return readback == level ? 0 : 1;
}

int cmdToggle(const Options& opt, const std::vector<std::string>& args) {
    if (args.empty()) {
        throw std::invalid_argument("用法：toggle <引脚>");
    }
    const unsigned pin = Pca9555::parsePin(args[0]);
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    if (dev.pinDirection(pin)) {
        dev.setPinOutput(pin, true);
        std::printf("%s 原为输入，已切换为输出 = 1\n", Pca9555::formatPin(pin).c_str());
    } else {
        dev.togglePin(pin);
        std::printf("%s 翻转完成，当前 = %d\n", Pca9555::formatPin(pin).c_str(),
                    dev.readPin(pin) ? 1 : 0);
    }
    return 0;
}

int cmdDir(const Options& opt, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        throw std::invalid_argument("用法：dir <引脚> <in|out>");
    }
    const unsigned pin = Pca9555::parsePin(args[0]);
    const bool as_input = parseInputFlag(args[1]);
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    dev.setPinDirection(pin, as_input);
    std::printf("%s 方向 = %s\n", Pca9555::formatPin(pin).c_str(), as_input ? "IN" : "OUT");
    return 0;
}

int cmdMonitor(const Options& opt, const std::vector<std::string>& args) {
    unsigned interval = 200;
    if (!args.empty()) {
        interval = parseNumber(args[0], "轮询间隔(ms)");
        if (interval < 20) {
            interval = 20;
        }
    }
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    std::printf("监视 %s 的输入引脚（每 %u ms 轮询，Ctrl+C 退出）\n",
                transport->describe().c_str(), interval);

    std::uint16_t last = dev.inputs();
    std::printf("[%s] 初始值 0x%04X  %s\n", timestamp().c_str(), last,
                binary16(last).c_str());
    while (!g_stop) {
        sleepMs(interval);
        const std::uint16_t now = dev.inputs();
        if (now == last) {
            continue;
        }
        for (unsigned pin = 0; pin < Pca9555::kPinCount; ++pin) {
            const bool before = (last >> pin) & 1u;
            const bool after = (now >> pin) & 1u;
            if (before != after) {
                std::printf("[%s] %s %s -> %s\n", timestamp().c_str(),
                            Pca9555::formatPin(pin).c_str(), before ? "1" : "0",
                            after ? "1" : "0");
                std::fflush(stdout);
            }
        }
        last = now;
    }
    std::printf("已退出监视。\n");
    return 0;
}

int cmdBlink(const Options& opt, const std::vector<std::string>& args) {
    if (args.empty()) {
        throw std::invalid_argument("用法：blink <引脚> [次数=10] [间隔ms=250]");
    }
    const unsigned pin = Pca9555::parsePin(args[0]);
    const unsigned count = args.size() > 1 ? parseNumber(args[1], "次数") : 10;
    const unsigned delay = args.size() > 2 ? parseNumber(args[2], "间隔(ms)") : 250;

    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    dev.setPinOutput(pin, false);
    std::printf("闪烁 %s 共 %u 次，间隔 %u ms\n", Pca9555::formatPin(pin).c_str(), count,
                delay);
    for (unsigned i = 0; i < count && !g_stop; ++i) {
        dev.togglePin(pin);
        std::printf("  第 %u 次 -> %d\n", i + 1, dev.readPin(pin) ? 1 : 0);
        std::fflush(stdout);
        sleepMs(delay);
    }
    dev.setPinOutput(pin, false);
    return 0;
}

int cmdSelfTest(const Options& opt) {
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    std::printf("自检 %s\n", transport->describe().c_str());

    const RegisterSnapshot before = readAll(*transport);
    std::printf("  原始 CFG=0x%04X OUT=0x%04X POL=0x%04X\n", before.config, before.outputs,
                before.polarity);

    bool ok = true;
    // 1) 切成全输入：此时输出寄存器怎么写都不会驱动引脚，是安全的测试环境
    dev.setDirection(Pca9555::kAllInput);
    if (dev.direction() != Pca9555::kAllInput) {
        std::printf("  [FAIL] 方向寄存器写回不一致\n");
        ok = false;
    } else {
        std::printf("  [ OK ] 方向寄存器可读写（全输入）\n");
    }

    // 2) 交叉写两个互补模式，验证 16 位锁存
    for (std::uint16_t pattern : {static_cast<std::uint16_t>(0xA5A5),
                                  static_cast<std::uint16_t>(0x5A5A)}) {
        dev.setOutputs(pattern);
        const std::uint16_t readback = dev.outputs();
        if (readback != pattern) {
            std::printf("  [FAIL] 输出锁存写入 0x%04X 读回 0x%04X\n", pattern, readback);
            ok = false;
        } else {
            std::printf("  [ OK ] 输出锁存 0x%04X 读回一致\n", pattern);
        }
    }

    // 3) 恢复原状态
    dev.configure(before.outputs, before.polarity, before.config);
    const RegisterSnapshot after = readAll(*transport);
    if (after.config != before.config || after.outputs != before.outputs) {
        std::printf("  [WARN] 恢复原状态后寄存器与初始值不同\n");
    } else {
        std::printf("  [ OK ] 已恢复原配置（CFG=0x%04X OUT=0x%04X）\n", after.config,
                    after.outputs);
    }
    std::printf("%s\n", ok ? "自检通过。" : "自检失败。");
    return ok ? 0 : 1;
}

int cmdDemo(const Options& opt) {
    std::unique_ptr<IRegisterTransport> transport = makeTransport(opt);
    Pca9555 dev(*transport);
    const RegisterSnapshot before = readAll(*transport);

    std::printf("=== PCA9555 演示 ===\n");
    std::printf("设备: %s\n", transport->describe().c_str());
    std::printf("原始状态: CFG=0x%04X OUT=0x%04X\n\n", before.config, before.outputs);

    std::printf("[1] 配置：P0.0-P0.7 = 输出（走马灯），P1.0-P1.7 = 输入\n");
    if (!opt.sim) {
        std::printf("    注意：会真实驱动 P0 口引脚，请确认外部电路允许。\n");
    }
    dev.configure(/*output=*/0xFF00, /*polarity=*/0x0000, /*direction=*/0xFF00);

    std::printf("[2] 走马灯：逐个点亮 P0.0 -> P0.7\n");
    for (unsigned i = 0; i < 8 && !g_stop; ++i) {
        const std::uint16_t mask = static_cast<std::uint16_t>(1u << i);
        dev.writePins(0x00FF, static_cast<std::uint16_t>(~mask & 0xFF));
        const std::uint16_t in = dev.inputs();
        std::printf("    %s 亮    输入 IN=0x%04X  %s\n", Pca9555::formatPin(i).c_str(), in,
                    binary16(in).c_str());
        std::fflush(stdout);
        sleepMs(150);
    }

    std::printf("[3] 读取 P1 口 8 个输入引脚\n");
    const std::uint16_t in = dev.inputs();
    for (unsigned pin = 8; pin < 16; ++pin) {
        std::printf("    %s = %d\n", Pca9555::formatPin(pin).c_str(), (in >> pin) & 1u);
    }

    std::printf("[4] 恢复原始配置\n");
    dev.configure(before.outputs, before.polarity, before.config);
    std::printf("    完成。\n");
    return 0;
}

void printHelp() {
    std::printf(
        "pca9555_tool - PCA9555 16 位 I2C IO 扩展器演示程序\n"
        "\n"
        "用法：pca9555_tool [全局选项] <子命令> [参数]\n"
        "\n"
        "全局选项：\n"
        "  --bus <N>        I2C 总线编号，也接受 i2c-7 / /dev/i2c-7\n"
        "  --addr <0xNN>    从机地址，默认 0x20（A2/A1/A0 全接地）\n"
        "  --sim            使用内存模型，不需要真实硬件\n"
        "  -v, --verbose    打印更多信息\n"
        "  -h, --help       显示本帮助\n"
        "\n"
        "子命令：\n"
        "  scan                     扫描所有总线的 0x20-0x27，找应答的设备\n"
        "  info                     打印全部寄存器与 16 个引脚的状态表\n"
        "  read                     读输入端口，打印 16 位电平\n"
        "  write <hex>              写输出锁存（0x0000-0xFFFF）\n"
        "  get <引脚>               读某引脚电平\n"
        "  set <引脚> <0|1>         把引脚切成输出并写电平\n"
        "  toggle <引脚>            翻转输出引脚\n"
        "  dir <引脚> <in|out>      设置引脚方向\n"
        "  monitor [间隔ms]         监视输入变化，Ctrl+C 退出\n"
        "  blink <引脚> [次数] [ms] 闪烁输出引脚\n"
        "  selftest                 非破坏性自检（寄存器读写回环）\n"
        "  demo                     完整演示：走马灯 + 读输入 + 恢复\n"
        "\n"
        "引脚写法：0-15（绝对编号）、p03、p1.2、p1_2、P17\n"
        "\n"
        "示例：\n"
        "  pca9555_tool scan                       # 先找设备在哪条总线\n"
        "  pca9555_tool --bus 7 info               # 看全部寄存器和引脚\n"
        "  pca9555_tool --bus 7 set p0.3 1         # 把 P0.3 拉高\n"
        "  pca9555_tool --bus 7 get p1.2           # 读 P1.2\n"
        "  pca9555_tool --bus 7 monitor            # 监视输入变化\n"
        "  pca9555_tool --sim demo                 # 无硬件跑一遍完整演示\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    Options opt;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(std::string(name) + " 需要一个参数");
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else if (arg == "--bus" || arg == "-b") {
            opt.bus = next("--bus");
        } else if (arg == "--addr" || arg == "-a") {
            const unsigned addr = parseNumber(next("--addr"), "从机地址");
            if (addr > 0x7F) {
                std::printf("从机地址必须 <= 0x7F\n");
                return 2;
            }
            opt.addr = static_cast<std::uint16_t>(addr);
        } else if (arg == "--sim") {
            opt.sim = true;
        } else if (arg == "-v" || arg == "--verbose") {
            opt.verbose = true;
        } else if (!arg.empty() && arg.front() == '-' && arg != "-") {
            std::printf("未知选项：%s（-h 查看帮助）\n", arg.c_str());
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) {
        printHelp();
        return 2;
    }

    const std::string command = positional[0];
    const std::vector<std::string> args(positional.begin() + 1, positional.end());

    try {
        if (command == "scan") {
            return cmdScan(opt);
        }
        if (command == "info") {
            return cmdInfo(opt);
        }
        if (command == "read") {
            return cmdRead(opt);
        }
        if (command == "write") {
            return cmdWrite(opt, args);
        }
        if (command == "get") {
            return cmdGet(opt, args);
        }
        if (command == "set") {
            return cmdSet(opt, args);
        }
        if (command == "toggle") {
            return cmdToggle(opt, args);
        }
        if (command == "dir") {
            return cmdDir(opt, args);
        }
        if (command == "monitor") {
            return cmdMonitor(opt, args);
        }
        if (command == "blink") {
            return cmdBlink(opt, args);
        }
        if (command == "selftest") {
            return cmdSelfTest(opt);
        }
        if (command == "demo") {
            return cmdDemo(opt);
        }
        std::printf("未知子命令：%s（-h 查看帮助）\n", command.c_str());
        return 2;
    } catch (const pca9555::I2cError& e) {
        std::printf("I2C 错误：%s\n", e.what());
        if (e.code() == 121 /* EREMOTEIO */ || e.code() == 6 /* ENXIO */) {
            std::printf("提示：从机没有应答。先用 scan 确认总线编号和地址，"
                        "并检查接线、上拉电阻、供电。\n");
        }
        return 1;
    } catch (const std::invalid_argument& e) {
        std::printf("参数错误：%s\n", e.what());
        return 2;
    } catch (const std::exception& e) {
        std::printf("失败：%s\n", e.what());
        return 1;
    }
}
