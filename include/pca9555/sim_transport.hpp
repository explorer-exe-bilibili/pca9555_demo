// SPDX-License-Identifier: MIT
/// @file sim_transport.hpp
/// @brief 内存版 PCA9555 模型，用于 --sim 演示和单元测试（无需真实硬件）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pca9555/transport.hpp"

namespace pca9555 {

/// 行为等价于一颗 PCA9555：
///  - 8 个寄存器，寄存器指针自动递增（与器件一致，超出 0x07 后回绕到 0x00）
///  - 引脚若为输入，IN 寄存器返回外部电平；若为输出，返回输出锁存驱动的电平
///  - IN 寄存器会应用极性反转
class SimTransport final : public IRegisterTransport {
public:
    SimTransport();

    void writeRegisters(std::uint8_t reg, const std::uint8_t* data,
                        std::size_t len) override;
    void readRegisters(std::uint8_t reg, std::uint8_t* data,
                       std::size_t len) override;
    std::string describe() const override { return "sim"; }

    /// 外部世界驱动的引脚电平（仅当该引脚配置为输入时有效）。
    void setPinLevel(unsigned pin, bool high);
    bool pinLevel(unsigned pin) const;

    /// 取得引脚的实际输出电平（配置为输出时由锁存决定，否则取外部电平）。
    bool effectiveLevel(unsigned pin) const;

    /// 直接看寄存器原始值（含未映射的 0x08 以上地址）。
    std::uint8_t rawRegister(std::uint8_t reg) const;

    /// 事务计数，用来验证驱动层是否把「寄存器指针 + 数据」合并成一次传输。
    unsigned writeTransactions() const noexcept { return write_tx_; }
    unsigned readTransactions() const noexcept { return read_tx_; }

    /// 注入故障，验证上层错误处理；传空串表示清除故障。
    void setFailure(const std::string& message);

private:
    void checkFailure() const;
    std::uint8_t& regRef(std::uint8_t reg);

    std::vector<std::uint8_t> regs_;
    bool external_level_[16];
    unsigned write_tx_{0};
    unsigned read_tx_{0};
    std::string failure_;
};

}  // namespace pca9555
