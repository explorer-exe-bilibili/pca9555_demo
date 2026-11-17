// SPDX-License-Identifier: MIT
/// @file i2c_bus.hpp
/// @brief Linux i2c-dev 传输实现（ioctl I2C_RDWR），不依赖 libi2c。
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "pca9555/transport.hpp"

namespace pca9555 {

/// 一个 /dev/i2c-N 上的从机连接。
///
/// 用 I2C_RDWR 一次性下发「寄存器指针 + 数据」，避免 SMBus 接口在
/// 部分控制器上不支持的问题；同时保持线程安全（同一对象跨线程使用）。
class I2cBus final : public IRegisterTransport {
public:
    /// 打开 device（如 "/dev/i2c-7"）并绑定 7 位地址 address（0x00-0x7F）。
    /// @throw I2cError 打开失败或地址非法
    I2cBus(std::string device, std::uint16_t address);
    ~I2cBus() override;

    I2cBus(const I2cBus&) = delete;
    I2cBus& operator=(const I2cBus&) = delete;

    void writeRegisters(std::uint8_t reg, const std::uint8_t* data,
                        std::size_t len) override;
    void readRegisters(std::uint8_t reg, std::uint8_t* data,
                       std::size_t len) override;
    std::string describe() const override;

    const std::string& device() const noexcept { return device_; }
    std::uint16_t address() const noexcept { return address_; }

    /// 扫描 /dev/i2c-*，返回可用设备路径（按编号排序）。
    static std::vector<std::string> listDevices();

    /// 探测某地址是否有从机应答。无应答返回 false，其余错误照常抛出。
    static bool probe(const std::string& device, std::uint16_t address);

    /// 适配器是否支持普通 I2C 传输（I2C_FUNC_I2C）。
    /// DDC/DP 之类的适配器往往只支持 SMBus，PCA9555 的连续读写走不通。
    static bool supportsI2cTransfer(const std::string& device);

    /// 该地址是否已被内核驱动绑定；是则返回驱动名，否则返回空串。
    static std::string boundDriver(const std::string& device, std::uint16_t address);

private:
    void checkAlive() const;

    std::string device_;
    std::uint16_t address_;
    int fd_{-1};
    std::mutex mutex_;
};

}  // namespace pca9555
