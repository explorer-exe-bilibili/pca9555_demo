// SPDX-License-Identifier: MIT
#include "pca9555/pca9555.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "pca9555/errors.hpp"

namespace pca9555 {
namespace {

unsigned parseUnsigned(const std::string& text) {
    if (text.empty() || text.size() > 3 ||
        text.find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("引脚号 \"" + text + "\" 不是合法十进制数");
    }
    return static_cast<unsigned>(std::stoul(text));
}

}  // namespace

Pca9555::Pca9555(IRegisterTransport& transport) : transport_(transport) {}

// ---------------------------------------------------------------- 整体寄存器

void Pca9555::writeWord(std::uint8_t reg, std::uint16_t value) {
    const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(value & 0xFF),
                                   static_cast<std::uint8_t>((value >> 8) & 0xFF)};
    transport_.writeRegisters(reg, bytes, 2);
}

std::uint16_t Pca9555::readWord(std::uint8_t reg) {
    std::uint8_t bytes[2] = {0, 0};
    transport_.readRegisters(reg, bytes, 2);
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8);
}

void Pca9555::refresh() {
    // 0x00..0x07 一次读完：输入 + 输出 + 极性 + 方向
    std::uint8_t regs[8] = {0};
    transport_.readRegisters(kRegInput0, regs, sizeof regs);
    outputs_ = static_cast<std::uint16_t>(regs[kRegOutput0]) |
               static_cast<std::uint16_t>(regs[kRegOutput1] << 8);
    polarity_ = static_cast<std::uint16_t>(regs[kRegPolarity0]) |
                static_cast<std::uint16_t>(regs[kRegPolarity1] << 8);
    direction_ = static_cast<std::uint16_t>(regs[kRegConfig0]) |
                 static_cast<std::uint16_t>(regs[kRegConfig1] << 8);
    cache_valid_ = true;
}

void Pca9555::ensureCache() {
    if (!cache_valid_) {
        refresh();
    }
}

void Pca9555::configure(std::uint16_t output_value, std::uint16_t polarity_mask,
                        std::uint16_t direction_mask) {
    // 顺序很重要：先把输出锁存写到位，再切方向，避免引脚上出现瞬时毛刺。
    writeWord(kRegOutput0, output_value);
    writeWord(kRegPolarity0, polarity_mask);
    writeWord(kRegConfig0, direction_mask);
    outputs_ = output_value;
    polarity_ = polarity_mask;
    direction_ = direction_mask;
    cache_valid_ = true;
}

std::uint16_t Pca9555::direction() {
    direction_ = readWord(kRegConfig0);
    cache_valid_ = true;
    return direction_;
}

void Pca9555::setDirection(std::uint16_t direction_mask) {
    writeWord(kRegConfig0, direction_mask);
    direction_ = direction_mask;
}

std::uint16_t Pca9555::polarity() {
    polarity_ = readWord(kRegPolarity0);
    return polarity_;
}

void Pca9555::setPolarity(std::uint16_t polarity_mask) {
    writeWord(kRegPolarity0, polarity_mask);
    polarity_ = polarity_mask;
}

std::uint16_t Pca9555::outputs() {
    outputs_ = readWord(kRegOutput0);
    return outputs_;
}

void Pca9555::setOutputs(std::uint16_t value) {
    writeWord(kRegOutput0, value);
    outputs_ = value;
}

std::uint16_t Pca9555::inputs() { return readWord(kRegInput0); }

// ---------------------------------------------------------------- 单引脚操作

void Pca9555::setPinDirection(unsigned pin, bool as_input) {
    if (pin >= kPinCount) {
        throw std::out_of_range("引脚号超出范围：" + std::to_string(pin));
    }
    ensureCache();
    const std::uint16_t bit = static_cast<std::uint16_t>(1u << pin);
    const std::uint16_t next = as_input ? (direction_ | bit) : (direction_ & ~bit);
    if (!as_input) {
        // 切成输出前先确认锁存值与影子寄存器一致，避免用旧值驱动引脚。
        writeWord(kRegOutput0, outputs_);
    }
    if (next != direction_) {
        writeWord(kRegConfig0, next);
        direction_ = next;
    }
}

void Pca9555::setPinOutput(unsigned pin, bool level) {
    if (pin >= kPinCount) {
        throw std::out_of_range("引脚号超出范围：" + std::to_string(pin));
    }
    ensureCache();
    const std::uint16_t bit = static_cast<std::uint16_t>(1u << pin);
    const std::uint16_t next_out = level ? (outputs_ | bit) : (outputs_ & ~bit);
    if (next_out != outputs_) {
        writeWord(kRegOutput0, next_out);
        outputs_ = next_out;
    }
    if (direction_ & bit) {
        const std::uint16_t next_dir = static_cast<std::uint16_t>(direction_ & ~bit);
        writeWord(kRegConfig0, next_dir);
        direction_ = next_dir;
    }
}

bool Pca9555::pinDirection(unsigned pin) {
    if (pin >= kPinCount) {
        throw std::out_of_range("引脚号超出范围：" + std::to_string(pin));
    }
    ensureCache();
    return (direction_ & (1u << pin)) != 0;
}

void Pca9555::writePin(unsigned pin, bool high) {
    if (pin >= kPinCount) {
        throw std::out_of_range("引脚号超出范围：" + std::to_string(pin));
    }
    ensureCache();
    const std::uint16_t bit = static_cast<std::uint16_t>(1u << pin);
    if (direction_ & bit) {
        throw std::logic_error(formatPin(pin) + " 当前配置为输入，不能直接写电平；" +
                               "请先 setPinOutput() 或 setPinDirection(pin,false)");
    }
    const std::uint16_t next = high ? (outputs_ | bit) : (outputs_ & ~bit);
    if (next != outputs_) {
        writeWord(kRegOutput0, next);
        outputs_ = next;
    }
}

bool Pca9555::readPin(unsigned pin) {
    if (pin >= kPinCount) {
        throw std::out_of_range("引脚号超出范围：" + std::to_string(pin));
    }
    return (inputs() & (1u << pin)) != 0;
}

void Pca9555::togglePin(unsigned pin) {
    if (pin >= kPinCount) {
        throw std::out_of_range("引脚号超出范围：" + std::to_string(pin));
    }
    ensureCache();
    const std::uint16_t bit = static_cast<std::uint16_t>(1u << pin);
    if (direction_ & bit) {
        throw std::logic_error(formatPin(pin) + " 当前配置为输入，无法翻转输出");
    }
    const std::uint16_t next = static_cast<std::uint16_t>(outputs_ ^ bit);
    writeWord(kRegOutput0, next);
    outputs_ = next;
}

void Pca9555::writePins(std::uint16_t mask, std::uint16_t values) {
    ensureCache();
    const std::uint16_t next =
        static_cast<std::uint16_t>((outputs_ & ~mask) | (values & mask));
    if (next != outputs_) {
        writeWord(kRegOutput0, next);
        outputs_ = next;
    }
}

// ---------------------------------------------------------------- 工具函数

std::string Pca9555::formatPin(unsigned pin) {
    if (pin >= kPinCount) {
        throw std::out_of_range("引脚号超出范围：" + std::to_string(pin));
    }
    return "P" + std::to_string(pin / 8) + "." + std::to_string(pin % 8);
}

unsigned Pca9555::parsePin(const std::string& text) {
    std::string s;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            s.push_back(c);
        }
    }
    if (s.empty()) {
        throw std::invalid_argument("引脚号为空");
    }

    bool has_prefix = (s[0] == 'p' || s[0] == 'P');
    if (has_prefix) {
        s.erase(s.begin());
    }
    if (s.empty()) {
        throw std::invalid_argument("引脚号 \"" + text + "\" 缺少数字");
    }

    // 带分隔符：p1.2 / p1_2 / p1-2
    const auto sep = s.find_first_of("._-");
    if (sep != std::string::npos) {
        const unsigned port = parseUnsigned(s.substr(0, sep));
        const unsigned bit = parseUnsigned(s.substr(sep + 1));
        if (port > 1 || bit > 7) {
            throw std::invalid_argument("引脚号 \"" + text + "\" 超出 P0.0-P1.7");
        }
        return port * 8 + bit;
    }

    const unsigned value = parseUnsigned(s);
    if (!has_prefix) {
        if (value >= kPinCount) {
            throw std::invalid_argument("引脚号 \"" + text + "\" 超出 0-15");
        }
        return value;
    }
    // 带前缀：p03 / p12 视为「端口+位」
    if (s.size() == 2) {
        const unsigned port = value / 10;
        const unsigned bit = value % 10;
        if (port > 1 || bit > 7) {
            throw std::invalid_argument("引脚号 \"" + text + "\" 超出 P0.0-P1.7");
        }
        return port * 8 + bit;
    }
    if (value < kPinCount) {
        return value;
    }
    throw std::invalid_argument("无法解析引脚号 \"" + text + "\"");
}

}  // namespace pca9555
