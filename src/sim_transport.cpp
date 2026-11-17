// SPDX-License-Identifier: MIT
#include "pca9555/sim_transport.hpp"

#include <cstring>

#include "pca9555/errors.hpp"
#include "pca9555/pca9555.hpp"

namespace pca9555 {
namespace {
constexpr std::size_t kRegisterSpace = 256;
}

SimTransport::SimTransport() : regs_(kRegisterSpace, 0) {
    // 上电复位值：CFG=0xFFFF（全输入）、OUT=0xFFFF、POL=0x0000
    regs_[Pca9555::kRegConfig0] = 0xFF;
    regs_[Pca9555::kRegConfig1] = 0xFF;
    regs_[Pca9555::kRegOutput0] = 0xFF;
    regs_[Pca9555::kRegOutput1] = 0xFF;
    for (bool& level : external_level_) {
        level = true;  // 输入引脚默认被内部 100uA 上拉拉高
    }
}

void SimTransport::checkFailure() const {
    if (!failure_.empty()) {
        throw I2cError(5 /* EIO */, "模拟故障：" + failure_);
    }
}

std::uint8_t& SimTransport::regRef(std::uint8_t reg) {
    return regs_[reg];
}

void SimTransport::writeRegisters(std::uint8_t reg, const std::uint8_t* data,
                                  std::size_t len) {
    checkFailure();
    ++write_tx_;
    unsigned addr = reg;
    for (std::size_t i = 0; i < len; ++i) {
        regs_[addr % kRegisterSpace] = data[i];
        ++addr;
    }
}

void SimTransport::readRegisters(std::uint8_t reg, std::uint8_t* data,
                                 std::size_t len) {
    checkFailure();
    ++read_tx_;

    const std::uint16_t latch = static_cast<std::uint16_t>(regs_[Pca9555::kRegOutput0]) |
                                static_cast<std::uint16_t>(regs_[Pca9555::kRegOutput1] << 8);
    const std::uint16_t dir = static_cast<std::uint16_t>(regs_[Pca9555::kRegConfig0]) |
                              static_cast<std::uint16_t>(regs_[Pca9555::kRegConfig1] << 8);
    const std::uint16_t pol = static_cast<std::uint16_t>(regs_[Pca9555::kRegPolarity0]) |
                              static_cast<std::uint16_t>(regs_[Pca9555::kRegPolarity1] << 8);

    // 引脚实际电平：输入看外部驱动，输出看锁存
    std::uint16_t pins = 0;
    for (unsigned i = 0; i < Pca9555::kPinCount; ++i) {
        const bool is_input = (dir & (1u << i)) != 0;
        const bool level = is_input ? external_level_[i] : ((latch >> i) & 1u) != 0;
        if (level) {
            pins = static_cast<std::uint16_t>(pins | (1u << i));
        }
    }
    pins = static_cast<std::uint16_t>(pins ^ pol);  // 极性反转只影响 IN 寄存器

    unsigned addr = reg;
    for (std::size_t i = 0; i < len; ++i) {
        if (addr == Pca9555::kRegInput0) {
            data[i] = static_cast<std::uint8_t>(pins & 0xFF);
        } else if (addr == Pca9555::kRegInput1) {
            data[i] = static_cast<std::uint8_t>((pins >> 8) & 0xFF);
        } else {
            data[i] = regs_[addr % kRegisterSpace];
        }
        ++addr;
    }
}

void SimTransport::setPinLevel(unsigned pin, bool high) {
    if (pin < Pca9555::kPinCount) {
        external_level_[pin] = high;
    }
}

bool SimTransport::pinLevel(unsigned pin) const {
    return pin < Pca9555::kPinCount ? external_level_[pin] : false;
}

bool SimTransport::effectiveLevel(unsigned pin) const {
    if (pin >= Pca9555::kPinCount) {
        return false;
    }
    const bool is_input = (regs_[Pca9555::kRegConfig0 + pin / 8] & (1u << (pin % 8))) != 0;
    if (is_input) {
        return external_level_[pin];
    }
    return (regs_[Pca9555::kRegOutput0 + pin / 8] & (1u << (pin % 8))) != 0;
}

std::uint8_t SimTransport::rawRegister(std::uint8_t reg) const {
    return regs_[reg];
}

void SimTransport::setFailure(const std::string& message) { failure_ = message; }

}  // namespace pca9555
