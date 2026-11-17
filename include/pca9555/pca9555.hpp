// SPDX-License-Identifier: MIT
/// @file pca9555.hpp
/// @brief PCA9555 16 位 I2C IO 扩展器驱动（纯用户态，无内核驱动依赖）。
///
/// 寄存器映射（数据手册 Table 2）：
///   0x00 IN0   / 0x01 IN1    输入端口（只读，反映引脚实际电平）
///   0x02 OUT0  / 0x03 OUT1   输出锁存（可读写）
///   0x04 POL0  / 0x05 POL1   极性反转（1 = 该位读回时取反）
///   0x06 CFG0  / 0x07 CFG1   方向（1 = 输入，0 = 输出）
///
/// 上电复位值：CFG=0xFFFF（全输入）、OUT=0xFFFF、POL=0x0000。
/// 注意 OUT 复位为 1，所以把某个引脚切成输出后若不再写值，它会立即输出高电平。
#pragma once

#include <cstdint>
#include <string>

#include "pca9555/transport.hpp"

namespace pca9555 {

class Pca9555 {
public:
    // ---- 寄存器地址 ----
    static constexpr std::uint8_t kRegInput0 = 0x00;
    static constexpr std::uint8_t kRegInput1 = 0x01;
    static constexpr std::uint8_t kRegOutput0 = 0x02;
    static constexpr std::uint8_t kRegOutput1 = 0x03;
    static constexpr std::uint8_t kRegPolarity0 = 0x04;
    static constexpr std::uint8_t kRegPolarity1 = 0x05;
    static constexpr std::uint8_t kRegConfig0 = 0x06;
    static constexpr std::uint8_t kRegConfig1 = 0x07;

    // ---- 地址范围 ----
    static constexpr std::uint8_t kBaseAddress = 0x20;  // A2=A1=A0=0
    static constexpr std::uint8_t kLastAddress = 0x27;  // A2=A1=A0=1

    static constexpr unsigned kPinCount = 16;
    static constexpr std::uint16_t kAllMask = 0xFFFF;
    static constexpr std::uint16_t kAllInput = 0xFFFF;

    /// 绑定一个传输后端（不接管其生命周期）。
    explicit Pca9555(IRegisterTransport& transport);

    // ================= 整体寄存器操作 =================

    /// 一次性配置：按 OUT → POL → CFG 的顺序写入（先让输出锁存就位再打开驱动，
    /// 避免引脚出现毛刺）。
    /// @param output_value   16 位输出电平（bit n = Pn）
    /// @param polarity_mask  1 = 读回取反
    /// @param direction_mask 1 = 输入，0 = 输出
    void configure(std::uint16_t output_value, std::uint16_t polarity_mask,
                   std::uint16_t direction_mask);

    /// 读方向寄存器（1 = 输入）。
    std::uint16_t direction();
    /// 写方向寄存器（1 = 输入）。
    void setDirection(std::uint16_t direction_mask);

    /// 读极性寄存器。
    std::uint16_t polarity();
    /// 写极性寄存器。
    void setPolarity(std::uint16_t polarity_mask);

    /// 读输出锁存（不是引脚电平；引脚若是输入则与硬件电平无关）。
    std::uint16_t outputs();
    /// 写输出锁存。
    void setOutputs(std::uint16_t value);

    /// 读输入端口，返回 16 位引脚电平（bit n = Pn，已含极性反转影响）。
    std::uint16_t inputs();

    /// 重新同步方向/极性/输出锁存三个影子寄存器。
    void refresh();

    // ================= 单引脚操作 =================

    /// 设置某引脚方向。切成输出时采用「先写锁存再切方向」的顺序。
    void setPinDirection(unsigned pin, bool as_input);
    /// 把引脚切成输出并立即给定电平（先写锁存、再切方向，不会出毛刺）。
    void setPinOutput(unsigned pin, bool level);
    /// 查询某引脚方向（true = 输入）。
    bool pinDirection(unsigned pin);

    /// 写某引脚电平（该引脚必须已是输出，否则抛 std::logic_error）。
    void writePin(unsigned pin, bool high);
    /// 读某引脚电平（引脚须为输入；输出引脚读回的是自身锁存驱动的电平）。
    bool readPin(unsigned pin);
    /// 翻转某引脚（须为输出）。
    void togglePin(unsigned pin);

    /// 按位批量写：只改动 mask 为 1 的位，其余保持不变（读-改-写落在内存里，
    /// 每次只发一次写事务）。
    void writePins(std::uint16_t mask, std::uint16_t values);

    // ================= 工具函数 =================

    /// 引脚号 -> "P0.3" / "P1.7"
    static std::string formatPin(unsigned pin);
    /// "p03" / "P0.3" / "3" / "P1_2" / "15" -> 引脚号 0..15
    /// @throw std::invalid_argument 解析失败
    static unsigned parsePin(const std::string& text);

    /// 传输后端描述，用于日志。
    std::string describe() const { return transport_.describe(); }

    const IRegisterTransport& transport() const noexcept { return transport_; }

private:
    void ensureCache();
    void writeWord(std::uint8_t reg, std::uint16_t value);
    std::uint16_t readWord(std::uint8_t reg);

    IRegisterTransport& transport_;
    std::uint16_t direction_{0};
    std::uint16_t outputs_{0};
    std::uint16_t polarity_{0};
    bool cache_valid_{false};
};

}  // namespace pca9555
