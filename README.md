# AV-player

[![C/C++ CI](https://github.com/Luoseyznl/Audio-Video-Player/actions/workflows/ci.yml/badge.svg)](https://github.com/Luoseyznl/Audio-Video-Player/actions)
![C++](https://img.shields.io/badge/C++-17-00599C.svg?style=flat&logo=c%2B%2B)
![CMake](https://img.shields.io/badge/CMake-3.14+-064F8C.svg?style=flat&logo=cmake)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3+-5586A4.svg?style=flat&logo=opengl)
![FFmpeg](https://img.shields.io/badge/FFmpeg-4.2+-007808.svg?style=flat)
![SDL2](https://img.shields.io/badge/SDL2-2.0+-173F5F.svg?style=flat)
![License](https://img.shields.io/badge/License-MIT-green.svg?style=flat)

A simple audio/video player engine based on FFmpeg, OpenGL, and SDL2.
The project follows a decoupled pipeline: Demuxing -> Decoding -> Buffering -> Rendering.

![Project Architecture](docs/01_Architecture_Overview/ffmepg_abstract.png)

[My Note of AV media](docs/01_Architecture_Overview/01_Architecture_Overview.md)

# Features

- Modular design with separate components for demuxing, decoding and rendering.
- Audio-driven synchronization logic with dynamic frame-dropping to handle CPU jitter.
- `SafeQueue` based producer-consumer model for packet/frame distribution.
- Built-in asynchronous logging system.
- Modern C++ Essentials
  - RAII: Resource management via smart pointers.
  - Concurrency: Lock-free state synchronization and fine-grained mutex control.

# Dependencies

- FFmpeg: Demuxing & Software/Hardware Decoding
- SDL2: Audio Output & Hardware Driver Management
- OpenGL/GLEW: Cross-platform Hardware-accelerated Rendering
- GLFW3: Windowing & Input Handling
- GTest: Unit Testing Framework

Install Dependencies (Ubuntu/Debian)

```sh
sudo apt-get update
sudo apt-get install -y cmake build-essential libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libsdl2-dev libglfw3-dev libglew-dev libgl1-mesa-dev
```

# Building & Installation

```sh
mkdir build & cd build
cmake ..
make install
```

# Usage

```sh
cd bin
./AVPlayer /path/to/your/video.mp4
```

Shortcuts:

- Space / P: Play / Pause
- Left / Right Arrow: Seek back(-10s) / forward(+10s)
- Up / Down Arrow: Volume Up / Down
- S: Step forward (when paused)
- R: Restart
- M: mute
- Q / Esc: quit

# Test

```sh
cd build
ctest
```
