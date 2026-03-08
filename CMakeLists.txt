cmake_minimum_required(VERSION 3.10)

project(tinyrtsp)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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

# Add executable
add_executable(HLS_Server ${SOURCES})
