cmake_minimum_required(VERSION 3.16)
project(pca9555_demo VERSION 1.0.0 LANGUAGES CXX)

# ------------------------------------------------------------------ 基本设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

option(PCA9555_BUILD_TESTS "构建单元测试（用内存模型，不需要硬件）" ON)
option(PCA9555_STATIC_LINK "静态链接可执行文件（目标板 glibc 较旧时更稳）" OFF)

# ------------------------------------------------------------------ 驱动库
add_library(pca9555 STATIC
    src/i2c_bus.cpp
    src/logging_transport.cpp
    src/pca9555.cpp
    src/sim_transport.cpp
)

target_include_directories(pca9555 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# i2c-dev / ioctl 是 Linux 专有接口
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "本工程依赖 Linux 的 /dev/i2c-* 接口，请在 WSL 或 Linux 下构建。")
endif()

find_package(Threads REQUIRED)
target_link_libraries(pca9555 PUBLIC Threads::Threads)

target_compile_options(pca9555 PRIVATE -Wall -Wextra -Wpedantic -Wshadow)

# ------------------------------------------------------------------ 命令行程序
add_executable(pca9555_tool apps/pca9555_tool.cpp)
target_link_libraries(pca9555_tool PRIVATE pca9555)
target_compile_options(pca9555_tool PRIVATE -Wall -Wextra -Wshadow)

if(PCA9555_STATIC_LINK)
    target_link_options(pca9555_tool PRIVATE -static)
    message(STATUS "PCA9555: 可执行文件将静态链接")
endif()

# ------------------------------------------------------------------ 单元测试
if(PCA9555_BUILD_TESTS)
    enable_testing()
    add_executable(pca9555_tests tests/test_pca9555.cpp)
    target_link_libraries(pca9555_tests PRIVATE pca9555)
    target_compile_options(pca9555_tests PRIVATE -Wall -Wextra -Wshadow)
    add_test(NAME unit_tests COMMAND pca9555_tests)
endif()

install(TARGETS pca9555_tool RUNTIME DESTINATION bin)

message(STATUS "PCA9555: 目标平台 ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}, "
               "构建类型 ${CMAKE_BUILD_TYPE}")
