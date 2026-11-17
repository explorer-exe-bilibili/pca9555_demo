# aarch64 交叉编译（板子是 RK3576 / Ubuntu 22.04，glibc 2.35；
# 构建机是 2.39，所以静态链接，拷过去直接跑）
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_EXE_LINKER_FLAGS "-static")
