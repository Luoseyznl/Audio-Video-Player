cmake_minimum_required(VERSION 3.14)
project(AVPlayer VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}")

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec # 解码器核心 (负责将 H.264 压缩数据解码成一帧帧原始画面)
    libavformat # 封装格式处理 (负责打开 MP4 文件，分离出独立的视频流和音频流)
    libavutil # 通用工具库 (提供内存分配、数学运算、随机数、日志等底层支持)
    libswscale # 视频图像转换 (负责图像缩放、像素格式转换，例如将 YUV 转换为 RGB)
    libswresample # 音频重采样 (负责转换音频的采样率和声道，以适配不同声卡的播放要求)
)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

find_package(OpenGL REQUIRED) # 系统级图形 API 接口
find_package(glfw3 REQUIRED) # 提供跨平台的窗口创建、上下文管理以及键盘/鼠标输入
find_package(GLEW REQUIRED) # 跨平台的 C++ 扩展库，帮助获取现代 OpenGL 的高级函数指针

# 开启 CTest 单元测试支持
enable_testing()

add_subdirectory(src)
add_subdirectory(examples)

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_executable(AVPlayer main.cc)

target_link_libraries(AVPlayer PRIVATE
    av_player_core
    PkgConfig::FFMPEG
    PkgConfig::SDL2
    OpenGL::GL
    glfw
    GLEW::GLEW
)

install(TARGETS AVPlayer DESTINATION bin)
