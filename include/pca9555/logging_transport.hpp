// SPDX-License-Identifier: MIT
/// @file logging_transport.hpp
/// @brief 传输层装饰器：把每次寄存器读写打到 stderr，用于接线/时序排查。
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "pca9555/transport.hpp"

namespace pca9555 {

class LoggingTransport final : public IRegisterTransport {
public:
    explicit LoggingTransport(std::unique_ptr<IRegisterTransport> inner);
    ~LoggingTransport() override;

    void writeRegisters(std::uint8_t reg, const std::uint8_t* data,
                        std::size_t len) override;
    void readRegisters(std::uint8_t reg, std::uint8_t* data, std::size_t len) override;
    std::string describe() const override;

private:
    std::unique_ptr<IRegisterTransport> inner_;
};

}  // namespace pca9555
