cmake_minimum_required(VERSION 3.10)

project(LiveLite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)           # 纯标准 C++，不要 GNU 扩展——Linux/macOS 行为一致

# Include directories
include_directories(include)

# Define source files
set(SOURCES
    src/HLS_Server.cpp
    src/HttpHlsSession.cpp
    src/EpollServer.cpp
    src/TcpConnection.cpp
    src/HttpFlvSession.cpp
    src/RtmpSession.cpp
    src/FlvReader.cpp
)

add_executable(LiveLite  ${SOURCES})
