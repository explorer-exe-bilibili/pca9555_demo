/**
 * @file    PCA9555.cpp
 * @brief   PCA9555 驱动实现
 */

#include "PCA9555.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iomanip>
#include <iostream>

// ================================================================
//  构造 / 析构
// ================================================================

PCA9555::PCA9555(const char* device, uint8_t address)
    : fd_(-1), addr_(address)
{
    fd_ = open(device, O_RDWR);
    if (fd_ < 0) {
        return;
    }

    if (ioctl(fd_, I2C_SLAVE, addr_) < 0) {
        close(fd_);
        fd_ = -1;
    }
}

PCA9555::~PCA9555()
{
    if (fd_ >= 0) {
        close(fd_);
    }
}

// ================================================================
//  I2C 底层
// ================================================================

// 从 reg 开始连续写 len 个字节: 寄存器指针 + 数据一并发出去
bool PCA9555::writeRegs(uint8_t reg, const uint8_t* buf, int len)
{
    if (fd_ < 0 || len < 1 || len > 2) {
        return false;
    }

    uint8_t tmp[3] = {reg, buf[0], (uint8_t)(len > 1 ? buf[1] : 0)};
    return write(fd_, tmp, (size_t)len + 1) == (ssize_t)(len + 1);
}

// 先写寄存器指针, 再连续读 len 个字节
bool PCA9555::readRegs(uint8_t reg, uint8_t* buf, int len)
{
    if (fd_ < 0 || len < 1) {
        return false;
    }
    if (write(fd_, &reg, 1) != 1) {
        return false;
    }
    return read(fd_, buf, (size_t)len) == (ssize_t)len;
}

// ================================================================
//  初始化
// ================================================================

bool PCA9555::begin()
{
    if (!isReady()) {
        return false;
    }

    uint8_t probe[2];
    if (!readRegs(REG_CFG0, probe, 2)) {   // 先确认芯片应答
        return false;
    }

    reset();
    return true;
}

void PCA9555::reset()
{
    const uint8_t all_in[2] = {0xFF, 0xFF};
    const uint8_t all_high[2] = {0xFF, 0xFF};
    const uint8_t no_invert[2] = {0x00, 0x00};

    writeRegs(REG_OUT0, all_high, 2);
    writeRegs(REG_POL0, no_invert, 2);
    writeRegs(REG_CFG0, all_in, 2);
}

// ================================================================
//  整组操作
// ================================================================

void PCA9555::setDirection(uint16_t input_mask)
{
    const uint8_t buf[2] = {(uint8_t)(input_mask & 0xFF), (uint8_t)(input_mask >> 8)};
    writeRegs(REG_CFG0, buf, 2);
}

uint16_t PCA9555::direction()
{
    uint8_t buf[2] = {0, 0};
    if (!readRegs(REG_CFG0, buf, 2)) {
        return 0xFFFF;
    }
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

void PCA9555::setPolarity(uint16_t mask)
{
    const uint8_t buf[2] = {(uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8)};
    writeRegs(REG_POL0, buf, 2);
}

uint16_t PCA9555::polarity()
{
    uint8_t buf[2] = {0, 0};
    if (!readRegs(REG_POL0, buf, 2)) {
        return 0xFFFF;
    }
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

void PCA9555::setOutput(uint16_t value)
{
    const uint8_t buf[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
    writeRegs(REG_OUT0, buf, 2);
}

uint16_t PCA9555::output()
{
    uint8_t buf[2] = {0, 0};
    if (!readRegs(REG_OUT0, buf, 2)) {
        return 0xFFFF;
    }
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

uint16_t PCA9555::input()
{
    uint8_t buf[2] = {0, 0};
    if (!readRegs(REG_IN0, buf, 2)) {
        return 0xFFFF;
    }
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

// ================================================================
//  单引脚操作
// ================================================================

void PCA9555::setPin(uint8_t pin, bool level)
{
    if (pin > 15) {
        return;
    }

    uint16_t out = output();
    if (level) {
        out |= (uint16_t)(1u << pin);
    } else {
        out &= (uint16_t)~(1u << pin);
    }
    setOutput(out);
}

bool PCA9555::getPin(uint8_t pin)
{
    if (pin > 15) {
        return false;
    }
    return ((input() >> pin) & 1u) != 0;
}

// ================================================================
//  调试
// ================================================================

void PCA9555::dumpRegisters()
{
    uint8_t r[8] = {0};
    if (!readRegs(REG_IN0, r, 8)) {
        std::cerr << "[PCA9555] 读寄存器失败" << std::endl;
        return;
    }

    auto hex = [](uint8_t v) {
        std::cout << "0x" << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << (int)v << std::dec << std::setfill(' ');
    };

    std::cout << "[PCA9555] 寄存器:" << std::endl;
    std::cout << "  IN  "; hex(r[0]); std::cout << " "; hex(r[1]); std::cout << std::endl;
    std::cout << "  OUT "; hex(r[2]); std::cout << " "; hex(r[3]); std::cout << std::endl;
    std::cout << "  POL "; hex(r[4]); std::cout << " "; hex(r[5]); std::cout << std::endl;
    std::cout << "  CFG "; hex(r[6]); std::cout << " "; hex(r[7]); std::cout << std::endl;
}
