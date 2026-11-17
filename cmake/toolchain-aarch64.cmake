# aarch64 交叉编译工具链（在 x86_64 的 WSL/Ubuntu 上编译给 RK3576 板子用）
#
# 用法：
#   cmake -S . -B build/aarch64 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake
# 依赖：
#   sudo apt install g++-aarch64-linux-gnu

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Debian/Ubuntu 的交叉工具链把目标库放在 /usr/aarch64-linux-gnu
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 构建机的 glibc (2.39) 比目标板 (Ubuntu 22.04, 2.35) 新，
# 默认静态链接，拷过去就能跑，不需要在板子上装任何运行库。
set(PCA9555_STATIC_LINK ON CACHE BOOL "交叉编译默认静态链接" FORCE)

set(CMAKE_C_FLAGS_INIT "-O2")
set(CMAKE_CXX_FLAGS_INIT "-O2")
