cmake_minimum_required(VERSION 3.14)
project(AVPlayer VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)

# 如果用户没有指定构建类型，则默认为 Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}")

find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)

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
    Threads::Threads
    PkgConfig::FFMPEG
    PkgConfig::SDL2
    OpenGL::GL
    glfw
    GLEW::GLEW
)

install(TARGETS AVPlayer DESTINATION bin)
