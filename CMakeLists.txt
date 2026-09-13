cmake_minimum_required(VERSION 3.10)

project(LiveLite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)           # 纯标准 C++，不要 GNU 扩展——Linux/macOS 行为一致

# CTest 注册表：等价 enable_testing()，额外提供 -DBUILD_TESTING=OFF 一键关掉测试构建
include(CTest)

if(BUILD_TESTING)
    add_compile_definitions(LITE_LIVE_TESTING)
endif()

# Include directories
include_directories(include)

# 平台无关源文件
set(COMMON_SOURCES
    src/HLS_Server.cpp
    src/HttpHlsSession.cpp
    src/LiveServer.cpp
    src/TcpConnection.cpp
    src/HttpFlvSession.cpp
    src/RtmpSession.cpp
    src/FlvReader.cpp
    src/ChunkReassembler.cpp
)
#按OS选Poller 实现平台相关
if(APPLE)
    set(POLLER_SOURCES src/KqueuePoller.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(POLLER_SOURCES src/EpollPoller.cpp)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}. Only Linux/macOS supported.")
endif()

# Poller 单独成一个静态库——这样能独立验证骨架（cmake --build . --target Poller），
# 只编平台相关的那一个 Poller 实现，不用连着整个服务器一起编
add_library(Poller STATIC ${POLLER_SOURCES})
target_include_directories(Poller PUBLIC include)

# 生成可执行文件
add_executable(LiveLite  ${COMMON_SOURCES})
target_link_libraries(LiveLite Poller)

# 生成测试可执行文件
add_executable(PollerSmokeTest tests/PollerSmokeTest.cpp)
target_link_libraries(PollerSmokeTest Poller)

if(APPLE)
    add_executable(KqueueProbe tests/kqueue_probe.cpp)
endif()

add_test(NAME PollerSmoke COMMAND PollerSmokeTest)
set_tests_properties(PollerSmoke PROPERTIES
    TIMEOUT 10
    LABELS "contract"
)
add_executable(PollerEchoTest tests/PollerEchoTest.cpp)
target_link_libraries(PollerEchoTest Poller)

add_executable(PollerContractTest tests/PollerContractTest.cpp)
target_link_libraries(PollerContractTest Poller)

add_test(NAME PollerContract COMMAND PollerContractTest)
set_tests_properties(PollerContract PROPERTIES
    TIMEOUT 10
    LABELS "contract"
)

add_executable(ConnectionLifecycleTest
                    src/TcpConnection.cpp
                    src/RtmpSession.cpp
                    src/HttpFlvSession.cpp
                    src/LiveServer.cpp
                    src/ChunkReassembler.cpp
                    tests/ConnectionLifecycleTest.cpp)
target_link_libraries(ConnectionLifecycleTest Poller)
add_test(NAME ConnectionLifecycle COMMAND ConnectionLifecycleTest)
set_tests_properties(ConnectionLifecycle PROPERTIES
    TIMEOUT 10
    LABELS "contract"
)


add_executable(RtmpHandshakeTest
                    src/TcpConnection.cpp
                    src/RtmpSession.cpp
                    src/HttpFlvSession.cpp
                    src/LiveServer.cpp
                    src/ChunkReassembler.cpp
                    tests/RtmpHandshakeTest.cpp)
target_link_libraries(RtmpHandshakeTest Poller)
add_test(NAME RtmpHandshake COMMAND RtmpHandshakeTest)
set_tests_properties(RtmpHandshake PROPERTIES
    TIMEOUT 10
    LABELS "contract"
)

add_executable(RtmpChunkTest
                    src/TcpConnection.cpp
                    src/RtmpSession.cpp
                    src/HttpFlvSession.cpp
                    src/LiveServer.cpp
                    src/ChunkReassembler.cpp
                    tests/RtmpChunkTest.cpp)
target_link_libraries(RtmpChunkTest Poller)
add_test(NAME RtmpChunk COMMAND RtmpChunkTest)
set_tests_properties(RtmpChunk PROPERTIES
    TIMEOUT 10
    LABELS "contract"
)
