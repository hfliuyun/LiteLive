cmake_minimum_required(VERSION 3.10)

project(LiveLite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)           # 纯标准 C++，不要 GNU 扩展——Linux/macOS 行为一致

# Include directories
include_directories(include)

# 平台无关源文件
set(COMMON_SOURCES
    src/HLS_Server.cpp
    src/HttpHlsSession.cpp
    src/EpollServer.cpp
    src/TcpConnection.cpp
    src/HttpFlvSession.cpp
    src/RtmpSession.cpp
    src/FlvReader.cpp
)
#按OS选Poller 实现平台相关
if(APPLE)
    set(POLLER_SOURCES src/KqueuePoller.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(POLLER_SOURCES src/EpollPoller.cpp)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}. Only Linux/macOS supported.")
endif()

# Poller 单独成一个静态库——这样 能独立验证骨架（cmake --build . --target Poller），
# 不被还没迁移、含 <sys/epoll.h> 的 EpollServer.cpp 拖垮
add_library(Poller STATIC ${POLLER_SOURCES})
target_include_directories(Poller PUBLIC include)

# 生成可执行文件
add_executable(LiveLite  ${COMMON_SOURCES})
target_link_libraries(LiveLite Poller)
