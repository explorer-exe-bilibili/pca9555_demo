// SPDX-License-Identifier: MIT
/// @file errors.hpp
/// @brief 异常类型定义：所有错误都从这里派生，便于上层统一捕获。
#pragma once

#include <stdexcept>
#include <string>

namespace pca9555 {

/// 通用错误。
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

/// I2C 总线层错误，保留 errno 以便排查（EIO=总线错误，EREMOTEIO=NACK 等）。
class I2cError : public Error {
public:
    I2cError(int code, const std::string& message)
        : Error(message), code_(code) {}

    /// 原始 errno。-1 表示非系统调用错误（例如参数非法）。
    int code() const noexcept { return code_; }

private:
    int code_;
};

}  // namespace pca9555
