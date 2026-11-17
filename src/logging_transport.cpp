// SPDX-License-Identifier: MIT
#include "pca9555/logging_transport.hpp"

#include <cstdio>
#include <utility>

namespace pca9555 {
namespace {

std::string hexBytes(const std::uint8_t* data, std::size_t len) {
    std::string text;
    char buf[8];
    for (std::size_t i = 0; i < len; ++i) {
        std::snprintf(buf, sizeof buf, " %02X", data[i]);
        text += buf;
    }
    return text;
}

}  // namespace

LoggingTransport::LoggingTransport(std::unique_ptr<IRegisterTransport> inner)
    : inner_(std::move(inner)) {}

LoggingTransport::~LoggingTransport() = default;

void LoggingTransport::writeRegisters(std::uint8_t reg, const std::uint8_t* data,
                                      std::size_t len) {
    inner_->writeRegisters(reg, data, len);
    std::fprintf(stderr, "[i2c] W %s reg 0x%02X <-%s\n", inner_->describe().c_str(), reg,
                 hexBytes(data, len).c_str());
}

void LoggingTransport::readRegisters(std::uint8_t reg, std::uint8_t* data,
                                     std::size_t len) {
    inner_->readRegisters(reg, data, len);
    std::fprintf(stderr, "[i2c] R %s reg 0x%02X ->%s\n", inner_->describe().c_str(), reg,
                 hexBytes(data, len).c_str());
}

std::string LoggingTransport::describe() const { return inner_->describe(); }

}  // namespace pca9555
