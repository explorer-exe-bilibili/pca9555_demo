// SPDX-License-Identifier: MIT
#include "pca9555/i2c_bus.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pca9555/errors.hpp"

namespace pca9555 {
namespace {

std::string hexText(unsigned value, int width = 2) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%0*X", width, value);
    return buf;
}

std::string errnoText(int code) {
    return std::string(std::strerror(code)) + " (errno=" + std::to_string(code) + ")";
}

/// NACK / 无此设备：属于「探测不到」，不算致命错误。
bool isNoDevice(int code) {
    return code == EREMOTEIO || code == ENXIO || code == ENODEV;
}

}  // namespace

I2cBus::I2cBus(std::string device, std::uint16_t address)
    : device_(std::move(device)), address_(address) {
    if (address_ > 0x7F) {
        throw I2cError(-1, "非法 7 位 I2C 地址 " + hexText(address_) + "（应为 0x00-0x7F）");
    }
    fd_ = ::open(device_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        const int err = errno;
        throw I2cError(err, "打开 " + device_ + " 失败：" + errnoText(err));
    }
}

I2cBus::~I2cBus() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void I2cBus::checkAlive() const {
    if (fd_ < 0) {
        throw I2cError(-1, device_ + " 已关闭");
    }
}

void I2cBus::writeRegisters(std::uint8_t reg, const std::uint8_t* data,
                            std::size_t len) {
    std::lock_guard<std::mutex> guard(mutex_);
    checkAlive();

    // 一次 I2C_RDWR：把「寄存器指针 + 数据」放在同一个写事务里，
    // PCA9555 会在收到指针后自动递增地址。
    std::uint8_t inline_buf[32];
    std::vector<std::uint8_t> heap_buf;
    std::uint8_t* buf = inline_buf;
    if (len + 1 > sizeof(inline_buf)) {
        heap_buf.resize(len + 1);
        buf = heap_buf.data();
    }
    buf[0] = reg;
    if (len > 0) {
        std::memcpy(buf + 1, data, len);
    }

    struct i2c_msg msg;
    std::memset(&msg, 0, sizeof msg);
    msg.addr = address_;
    msg.flags = 0;
    msg.len = static_cast<std::uint16_t>(len + 1);
    msg.buf = buf;

    struct i2c_rdwr_ioctl_data xfer;
    std::memset(&xfer, 0, sizeof xfer);
    xfer.msgs = &msg;
    xfer.nmsgs = 1;

    int rc;
    do {
        rc = ::ioctl(fd_, I2C_RDWR, &xfer);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        const int err = errno;
        throw I2cError(err, "写 " + describe() + " 寄存器 " + hexText(reg) + " 失败：" +
                                errnoText(err));
    }
}

void I2cBus::readRegisters(std::uint8_t reg, std::uint8_t* data, std::size_t len) {
    std::lock_guard<std::mutex> guard(mutex_);
    checkAlive();

    struct i2c_msg msgs[2];
    std::memset(msgs, 0, sizeof msgs);

    // 第一段：写寄存器指针（不产生 STOP 由控制器决定，i2c-dev 用重复起始即可）
    msgs[0].addr = address_;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg;

    // 第二段：连续读 len 字节
    msgs[1].addr = address_;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = static_cast<std::uint16_t>(len);
    msgs[1].buf = data;

    struct i2c_rdwr_ioctl_data xfer;
    std::memset(&xfer, 0, sizeof xfer);
    xfer.msgs = msgs;
    xfer.nmsgs = 2;

    int rc;
    do {
        rc = ::ioctl(fd_, I2C_RDWR, &xfer);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        const int err = errno;
        throw I2cError(err, "读 " + describe() + " 寄存器 " + hexText(reg) + " 失败：" +
                                errnoText(err));
    }
}

std::string I2cBus::describe() const {
    return device_ + "@" + hexText(address_);
}

std::vector<std::string> I2cBus::listDevices() {
    std::vector<std::pair<int, std::string>> found;
    DIR* dir = ::opendir("/dev");
    if (dir != nullptr) {
        while (struct dirent* entry = ::readdir(dir)) {
            const std::string name(entry->d_name);
            if (name.rfind("i2c-", 0) != 0) {
                continue;
            }
            const std::string number = name.substr(4);
            if (number.empty() ||
                number.find_first_not_of("0123456789") != std::string::npos) {
                continue;
            }
            found.emplace_back(std::atoi(number.c_str()), "/dev/" + name);
        }
        ::closedir(dir);
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::string> devices;
    devices.reserve(found.size());
    for (const auto& item : found) {
        devices.push_back(item.second);
    }
    return devices;
}

bool I2cBus::probe(const std::string& device, std::uint16_t address) {    const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        throw I2cError(err, "打开 " + device + " 失败：" + errnoText(err));
    }

    // 只读 1 字节寄存器 0：对 PCA9555 无副作用，从机不应答时内核报 EREMOTEIO。
    std::uint8_t reg = 0;
    std::uint8_t value = 0;
    struct i2c_msg msgs[2];
    std::memset(msgs, 0, sizeof msgs);
    msgs[0].addr = address;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = &reg;
    msgs[1].addr = address;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len = 1;
    msgs[1].buf = &value;

    struct i2c_rdwr_ioctl_data xfer;
    std::memset(&xfer, 0, sizeof xfer);
    xfer.msgs = msgs;
    xfer.nmsgs = 2;

    int rc;
    do {
        rc = ::ioctl(fd, I2C_RDWR, &xfer);
    } while (rc < 0 && errno == EINTR);
    const int err = errno;
    ::close(fd);

    if (rc < 0) {
        if (isNoDevice(err)) {
            return false;
        }
        throw I2cError(err, "探测 " + device + " 地址 " + hexText(address) + " 失败：" +
                                errnoText(err));
    }
    return true;
}

bool I2cBus::supportsI2cTransfer(const std::string& device) {
    const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        throw I2cError(err, "打开 " + device + " 失败：" + errnoText(err));
    }
    unsigned long funcs = 0;
    const int rc = ::ioctl(fd, I2C_FUNCS, &funcs);
    ::close(fd);
    if (rc < 0) {
        return false;  // 不响应 I2C_FUNCS 的适配器（比如部分 DDC）按不支持处理
    }
    return (funcs & I2C_FUNC_I2C) != 0;
}

std::string I2cBus::boundDriver(const std::string& device, std::uint16_t address) {
    // 从 "/dev/i2c-7" 里取出总线号
    const std::string prefix = "/dev/i2c-";
    if (device.rfind(prefix, 0) != 0) {
        return std::string();
    }
    const int bus = std::atoi(device.c_str() + prefix.size());
    char path[128];
    std::snprintf(path, sizeof path, "/sys/bus/i2c/devices/i2c-%d/%d-%04x/name", bus, bus,
                  address);

    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::string();
    }
    char name[64] = {0};
    const ssize_t got = ::read(fd, name, sizeof name - 1);
    ::close(fd);
    if (got <= 0) {
        return std::string();
    }
    std::string text(name);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

}  // namespace pca9555
