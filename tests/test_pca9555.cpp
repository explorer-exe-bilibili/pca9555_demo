// SPDX-License-Identifier: MIT
/// @file test_pca9555.cpp
/// @brief 驱动逻辑单元测试：用 SimTransport 模拟芯片，在 x86 上直接跑，不需要硬件。
#include <cstdio>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "pca9555/errors.hpp"
#include "pca9555/pca9555.hpp"
#include "pca9555/sim_transport.hpp"

namespace {

// ---------------------------------------------------------------- 迷你测试框架

struct TestCase {
    std::string name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

int g_checks = 0;
int g_failures = 0;
const char* g_current = "";

[[maybe_unused]] void report(bool ok, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("    [FAIL] %s:%d  %s  (%s)\n", file, line, expr, g_current);
    }
}

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const char* expr, const char* file,
                int line) {
    ++g_checks;
    using Common = std::common_type_t<A, B>;
    if (!(static_cast<Common>(actual) == static_cast<Common>(expected))) {
        ++g_failures;
        std::printf("    [FAIL] %s:%d  %s  (%s)\n", file, line, expr, g_current);
    }
}

template <typename Exception, typename Fn>
void checkThrows(Fn&& fn, const char* expr, const char* file, int line) {
    ++g_checks;
    try {
        fn();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& e) {
        ++g_failures;
        std::printf("    [FAIL] %s:%d  %s 抛出了别的异常：%s  (%s)\n", file, line, expr,
                    e.what(), g_current);
        return;
    }
    ++g_failures;
    std::printf("    [FAIL] %s:%d  %s 没有抛异常  (%s)\n", file, line, expr, g_current);
}

#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#define CHECK_EQ(actual, expected) \
    checkEqual((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)

#define CHECK_THROWS(type, expr) checkThrows<type>([&] { expr; }, #expr, __FILE__, __LINE__)

/// TEST("用例说明") { ... }
#define TEST_CONCAT_(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_(a, b)
#define TEST(description)                                                  \
    static void TEST_CONCAT(test_body_, __LINE__)();                       \
    static Registrar TEST_CONCAT(registrar_, __LINE__)(                    \
        description, TEST_CONCAT(test_body_, __LINE__));                   \
    static void TEST_CONCAT(test_body_, __LINE__)()

using pca9555::I2cError;
using pca9555::Pca9555;
using pca9555::SimTransport;

// ---------------------------------------------------------------- 设备语义

TEST("上电复位状态与数据手册一致：全输入、锁存全 1、极性不反转") {
    SimTransport bus;
    Pca9555 dev(bus);
    CHECK_EQ(dev.direction(), 0xFFFFu);
    CHECK_EQ(dev.outputs(), 0xFFFFu);
    CHECK_EQ(dev.polarity(), 0x0000u);
    CHECK_EQ(dev.inputs(), 0xFFFFu);  // 输入默认被内部上拉拉高
}

TEST("写输出锁存：低字节进 OUT0、高字节进 OUT1") {
    SimTransport bus;
    Pca9555 dev(bus);
    dev.setOutputs(0x1234);
    CHECK_EQ(dev.outputs(), 0x1234u);
    CHECK_EQ(bus.rawRegister(Pca9555::kRegOutput0), 0x34u);
    CHECK_EQ(bus.rawRegister(Pca9555::kRegOutput1), 0x12u);
}

TEST("单引脚切成输出后能驱动电平，且不影响相邻引脚") {
    SimTransport bus;
    Pca9555 dev(bus);
    dev.setPinOutput(3, true);
    CHECK_EQ(dev.pinDirection(3), false);
    CHECK_EQ(dev.readPin(3), true);
    dev.writePin(3, false);
    CHECK_EQ(dev.readPin(3), false);
    CHECK_EQ(dev.readPin(4), true);
}

TEST("对输入引脚写电平或翻转会被拒绝（std::logic_error）") {
    SimTransport bus;
    Pca9555 dev(bus);
    CHECK_EQ(dev.pinDirection(5), true);
    CHECK_THROWS(std::logic_error, dev.writePin(5, true));
    CHECK_THROWS(std::logic_error, dev.togglePin(5));
}

TEST("翻转输出引脚") {
    SimTransport bus;
    Pca9555 dev(bus);
    dev.setPinOutput(10, false);
    dev.togglePin(10);
    CHECK_EQ(dev.readPin(10), true);
    dev.togglePin(10);
    CHECK_EQ(dev.readPin(10), false);
}

TEST("输入引脚反映外部电平；切成输出后外部电平不再影响读数") {
    SimTransport bus;
    Pca9555 dev(bus);
    bus.setPinLevel(12, false);
    CHECK_EQ(dev.readPin(12), false);
    CHECK_EQ(dev.inputs() & (1u << 12), 0u);

    bus.setPinLevel(12, true);
    CHECK_EQ(dev.readPin(12), true);

    dev.setPinOutput(12, false);
    bus.setPinLevel(12, true);
    CHECK_EQ(dev.readPin(12), false);
}

TEST("极性反转只影响 IN 寄存器读回，不影响输出锁存") {
    SimTransport bus;
    Pca9555 dev(bus);
    bus.setPinLevel(0, true);
    bus.setPinLevel(1, false);
    CHECK_EQ(dev.inputs() & 0x0003u, 0x0001u);

    dev.setPolarity(0x0003);
    CHECK_EQ(dev.inputs() & 0x0003u, 0x0002u);
    CHECK_EQ(dev.outputs(), 0xFFFFu);
}

TEST("configure 依次写 OUT/POL/CFG，三笔事务后寄存器落地") {
    SimTransport bus;
    Pca9555 dev(bus);
    dev.configure(0x00FF, 0x0000, 0xFF00);
    CHECK_EQ(dev.outputs(), 0x00FFu);
    CHECK_EQ(dev.direction(), 0xFF00u);
    CHECK_EQ(bus.rawRegister(Pca9555::kRegConfig0), 0x00u);
    CHECK_EQ(bus.rawRegister(Pca9555::kRegConfig1), 0xFFu);
    CHECK_EQ(bus.writeTransactions(), 3u);
}

TEST("refresh 用一次读事务取回全部寄存器") {
    SimTransport bus;
    Pca9555 dev(bus);
    const unsigned before = bus.readTransactions();
    dev.refresh();
    CHECK_EQ(bus.readTransactions(), before + 1);
    CHECK_EQ(dev.outputs(), 0xFFFFu);
}

TEST("writePins 只改动掩码内的位") {
    SimTransport bus;
    Pca9555 dev(bus);
    dev.setOutputs(0x0000);
    dev.writePins(0x00F0, 0x0050);
    CHECK_EQ(dev.outputs(), 0x0050u);
    dev.writePins(0x0003, 0x0003);
    CHECK_EQ(dev.outputs(), 0x0053u);
}

TEST("引脚越界被拒绝（std::out_of_range）") {
    SimTransport bus;
    Pca9555 dev(bus);
    CHECK_THROWS(std::out_of_range, dev.writePin(16, true));
    CHECK_THROWS(std::out_of_range, dev.readPin(99));
    CHECK_THROWS(std::out_of_range, Pca9555::formatPin(16));
}

TEST("引脚名解析与格式化") {
    CHECK_EQ(Pca9555::formatPin(0), std::string("P0.0"));
    CHECK_EQ(Pca9555::formatPin(7), std::string("P0.7"));
    CHECK_EQ(Pca9555::formatPin(8), std::string("P1.0"));
    CHECK_EQ(Pca9555::formatPin(15), std::string("P1.7"));

    CHECK_EQ(Pca9555::parsePin("0"), 0u);
    CHECK_EQ(Pca9555::parsePin("15"), 15u);
    CHECK_EQ(Pca9555::parsePin("p03"), 3u);
    CHECK_EQ(Pca9555::parsePin("p12"), 10u);
    CHECK_EQ(Pca9555::parsePin("P1.2"), 10u);
    CHECK_EQ(Pca9555::parsePin("p1_2"), 10u);
    CHECK_EQ(Pca9555::parsePin("p17"), 15u);
    CHECK_EQ(Pca9555::parsePin("p7"), 7u);

    CHECK_THROWS(std::invalid_argument, Pca9555::parsePin("16"));
    CHECK_THROWS(std::invalid_argument, Pca9555::parsePin("p28"));
    CHECK_THROWS(std::invalid_argument, Pca9555::parsePin(""));
    CHECK_THROWS(std::invalid_argument, Pca9555::parsePin("abc"));
}

TEST("传输层故障会原样向上抛出") {
    SimTransport bus;
    Pca9555 dev(bus);
    bus.setFailure("总线掉了");
    CHECK_THROWS(I2cError, dev.inputs());
    bus.setFailure("");
    CHECK_EQ(dev.inputs(), 0xFFFFu);
}

TEST("寄存器指针自动递增") {
    SimTransport bus;
    const std::uint8_t pattern[3] = {0x11, 0x22, 0x33};
    bus.writeRegisters(Pca9555::kRegPolarity0, pattern, sizeof pattern);
    CHECK_EQ(bus.rawRegister(0x04), 0x11u);
    CHECK_EQ(bus.rawRegister(0x05), 0x22u);
    CHECK_EQ(bus.rawRegister(0x06), 0x33u);

    std::uint8_t out[2] = {0, 0};
    bus.readRegisters(Pca9555::kRegOutput0, out, sizeof out);
    CHECK_EQ(out[0], 0xFFu);
    CHECK_EQ(out[1], 0xFFu);
}

TEST("一次读 8 字节覆盖 IN/OUT/POL/CFG 的完整快照") {
    SimTransport bus;
    Pca9555 dev(bus);
    dev.configure(0x55AA, 0x0000, 0x00FF);
    std::uint8_t regs[8] = {0};
    bus.readRegisters(Pca9555::kRegInput0, regs, sizeof regs);
    CHECK_EQ(regs[Pca9555::kRegOutput0], 0xAAu);
    CHECK_EQ(regs[Pca9555::kRegOutput1], 0x55u);
    CHECK_EQ(regs[Pca9555::kRegConfig0], 0xFFu);
    CHECK_EQ(regs[Pca9555::kRegConfig1], 0x00u);
}

}  // namespace

int main() {
    std::printf("运行 %zu 个测试用例\n\n", registry().size());
    for (const TestCase& test : registry()) {
        g_current = test.name.c_str();
        const int before = g_failures;
        test.fn();
        std::printf("%s %s\n", g_failures == before ? "[PASS]" : "[FAIL]", test.name.c_str());
    }
    std::printf("\n%d 项断言，%d 项失败\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
