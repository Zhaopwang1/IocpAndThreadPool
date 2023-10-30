cmake_minimum_required(VERSION 3.16)

# 项目名称，Windows平台需要
project(IocpAndThreadPool)

# -g 调试
# -Wall 所有警告开启
# -O0 优化
# -Who-unused-variable 为用到的变量无需警告（避免开发阶段警告过多）
# pthread 多线程
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -Wall -O0 -Wno-unused-variable -pthread")


set(srcs
    Thread.h
    ThreadQueue.h
    Server.cpp
    Tools.h
)


# 编译
add_executable(IocpAndThreadPool ${srcs})


# 链接
target_link_libraries(IocpAndThreadPool)
