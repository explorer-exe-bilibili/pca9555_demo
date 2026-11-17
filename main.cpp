/**
 * @file    main.cpp
 * @brief   PCA9555 演示程序
 *
 * 功能:
 *  - P0.0 ~ P0.7 配成输出, 做走马灯 (低电平点亮)
 *  - P1.0 ~ P1.7 配成输入, 每一步打印一次按键状态
 *  - 循环保持, Ctrl+C 安全退出并恢复全输入
 */

#include "PCA9555.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <thread>

// ---- 全局变量 (信号处理用) ----
static std::atomic<bool> g_running{true};

void onSignal([[maybe_unused]] int sig)
{
    std::cout << "\n[main] 收到退出信号, 安全关闭中..." << std::endl;
    g_running = false;
}

// 打印 P1 口 8 个输入
static void printInputs(PCA9555& io)
{
    uint16_t in = io.input();
    std::cout << "P1 = ";
    for (int i = 15; i >= 8; --i) {
        std::cout << (((in >> i) & 1u) ? '1' : '0');
    }
    std::cout << "  (IN=0x" << std::hex << std::setw(4) << std::setfill('0')
              << in << std::dec << std::setfill(' ') << ")" << std::endl;
}

// ================================================================
int main()
{
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    // ---- 参数 ----
    const char*       DEVICE  = "/dev/i2c-7";
    constexpr uint8_t ADDRESS = 0x20;

    // ---- 初始化 ----
    PCA9555 io(DEVICE, ADDRESS);
    if (!io.isReady()) {
        std::cerr << "[main] 打开 " << DEVICE << " 失败, 用 i2cdetect -y 7 确认总线" << std::endl;
        return 1;
    }
    if (!io.begin()) {
        std::cerr << "[main] 地址 0x20 无应答, 检查接线/上拉/A0~A2 (i2cdetect -y 7)" << std::endl;
        return 1;
    }

    io.dumpRegisters();          // 验证寄存器读写

    // ---- 配置: P0 输出, P1 输入 ----
    io.setOutput(0x00FF);        // 输出锁存: P0 全低, P1 保持高
    io.setDirection(0xFF00);     // 先写锁存再切方向, 避免毛刺

    std::cout << "\n============ 走马灯 + 读输入 ============" << std::endl;

    int step = 0;
    while (g_running) {
        // 只有当前位为低, 其余为高 (P1 的锁存位保持 1)
        io.setOutput((uint16_t)(0xFFFF ^ (1u << step)));

        std::cout << "  P0." << step << " 点亮   ";
        printInputs(io);

        step = (step + 1) % 8;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // ---- 安全退出: 恢复全输入 ----
    std::cout << "[main] 恢复为全输入..." << std::endl;
    io.setOutput(0xFFFF);
    io.setDirection(0xFFFF);

    std::cout << "[main] 程序结束." << std::endl;
    return 0;
}
