/**
 * @file    PCA9555.hpp
 * @brief   PCA9555 16路 IO 扩展器驱动 (I2C)
 *
 * @details 通过 Linux I2C 子系统驱动 PCA9555 芯片。
 *          支持:
 *          - 16 路 IO 独立配置输入/输出
 *          - 单引脚 / 整组读写
 *          - 极性反转
 *          - 寄存器转储调试
 *
 * @note    上电默认 CFG=0xFFFF (全输入), OUT=0xFFFF, POL=0x0000。
 *          某引脚切成输出的瞬间会输出锁存里的值, 所以先写锁存再切方向。
 *
 * 硬件连接示例:
 * @code
 *   PCA9555            主控 (LubanCat-3IO / ARM板)
 *   ┌──────────┐      ┌──────────────────┐
 *   │ SDA ─────┼──────┤ 40pin 第 3 脚     │
 *   │ SCL ─────┼──────┤ 40pin 第 5 脚     │
 *   │ VCC ─────┼──────┤ 3.3V             │
 *   │ GND ─────┼──────┤ GND              │
 *   │ A0~A2 ───┼──────┤ GND  (地址 0x20)  │
 *   └──────────┘      └──────────────────┘
 * @endcode
 */

#ifndef PCA9555_HPP
#define PCA9555_HPP

#include <cstdint>

class PCA9555 {
public:
    // ---- 寄存器地址 ----
    enum {
        REG_IN0  = 0x00, REG_IN1  = 0x01,   // 输入电平, 只读
        REG_OUT0 = 0x02, REG_OUT1 = 0x03,   // 输出锁存
        REG_POL0 = 0x04, REG_POL1 = 0x05,   // 极性反转, 1 = 读回取反
        REG_CFG0 = 0x06, REG_CFG1 = 0x07,   // 方向, 1 = 输入, 0 = 输出
    };

    // ================================================================
    //  构造 / 析构
    // ================================================================

    /**
     * @brief 打开 I2C 设备并绑定从机地址
     * @param device   I2C 总线设备节点, 如 "/dev/i2c-7"
     * @param address  7位 I2C 地址, 默认 0x20
     */
    explicit PCA9555(const char* device, uint8_t address = 0x20);

    /** @brief 关闭 I2C 设备 */
    ~PCA9555();

    // 禁止拷贝 (I2C fd 独占)
    PCA9555(const PCA9555&)            = delete;
    PCA9555& operator=(const PCA9555&) = delete;

    // ================================================================
    //  初始化
    // ================================================================

    /** @brief I2C 是否成功打开 */
    bool isReady() const { return fd_ >= 0; }

    /**
     * @brief   复位到上电默认并确认芯片在线
     * @details 写 CFG=0xFFFF / OUT=0xFFFF / POL=0x0000, 然后回读寄存器。
     * @return  true 成功, false 芯片无应答
     */
    bool begin();

    // ================================================================
    //  整组操作
    // ================================================================

    /**
     * @brief 设置方向
     * @param input_mask 1 = 输入, 0 = 输出 (bit0 = P0.0 ... bit15 = P1.7)
     */
    void setDirection(uint16_t input_mask);
    uint16_t direction();

    /** @brief 极性反转: mask 中 1 的引脚读回取反 */
    void setPolarity(uint16_t mask);
    uint16_t polarity();

    /** @brief 写 16 位输出锁存 */
    void setOutput(uint16_t value);
    uint16_t output();

    /** @brief 读 16 位引脚电平 */
    uint16_t input();

    // ================================================================
    //  单引脚操作
    // ================================================================

    /**
     * @brief 写单引脚 (读回锁存后只改一位, 不影响其他引脚)
     * @note  该引脚应已通过 setDirection() 配成输出
     */
    void setPin(uint8_t pin, bool level);

    /** @brief 读单引脚电平 */
    bool getPin(uint8_t pin);

    // ================================================================
    //  调试
    // ================================================================

    /** @brief 打印 8 个寄存器到 stdout */
    void dumpRegisters();

    /** @brief 恢复上电默认: 全输入, 锁存全 1, 极性不反转 */
    void reset();

private:
    bool writeRegs(uint8_t reg, const uint8_t* buf, int len);
    bool readRegs(uint8_t reg, uint8_t* buf, int len);

    int     fd_;
    uint8_t addr_;
};

#endif  // PCA9555_HPP
