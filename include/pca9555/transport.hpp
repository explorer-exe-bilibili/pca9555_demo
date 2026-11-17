// SPDX-License-Identifier: MIT
/// @file transport.hpp
/// @brief 寄存器访问抽象层。
///
/// PCA9555 的协议是「先写 1 字节寄存器指针，再连续读写 N 字节数据」，
/// 寄存器指针在每次传输后自动递增，因此这里只抽象两种操作：
///   - writeRegisters(reg, data, len)
///   - readRegisters (reg, data, len)
/// 具体走 /dev/i2c-N（I2C_RDWR ioctl）、还是内存模型（单元测试 / --sim），
/// 由实现类决定，驱动层 Pca9555 不感知。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pca9555 {

class IRegisterTransport {
public:
    virtual ~IRegisterTransport() = default;

    /// 从 reg 开始连续写 len 个字节（寄存器指针自动递增）。
    virtual void writeRegisters(std::uint8_t reg, const std::uint8_t* data,
                                std::size_t len) = 0;

    /// 从 reg 开始连续读 len 个字节（寄存器指针自动递增）。
    virtual void readRegisters(std::uint8_t reg, std::uint8_t* data,
                               std::size_t len) = 0;

    /// 供日志/报错使用的描述，例如 "/dev/i2c-7@0x20" 或 "sim"。
    virtual std::string describe() const = 0;

protected:
    IRegisterTransport() = default;
    IRegisterTransport(const IRegisterTransport&) = default;
    IRegisterTransport& operator=(const IRegisterTransport&) = default;
};

}  // namespace pca9555
