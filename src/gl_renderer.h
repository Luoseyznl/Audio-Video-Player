#pragma once

// 先包含 glew，再包含 glfw
#include <GL/glew.h>     // OpenGL Extension Wrangler Library
#include <GLFW/glfw3.h>  // Graphics Library Framework

#include "mediadefs.h"

namespace avplayer {

/**
 * GLFW: 负责与操作系统打交道，创建系统窗口、管理 OpenGL 上下文、处理键鼠事件
 * GLEW: 负责与显卡驱动打交道，动态加载现代 OpenGL 函数指针
 * GPU 渲染管线核心对象（所有 GLuint 均为显存对象的句柄）
 * 1. 控制
 * ├── shader_program_: 着色器主程序，链接了下面两个着色器
 * ├── 顶点着色器 (Vertex Shader): 确定视频画面在窗口中的几何位置和拉伸形变
 * └── 片段着色器 (Fragment Shader): 负责采样 YUV 纹理，运算转换为 RGB 像素
 * 2. 数据
 * ├── vbo_: 顶点缓冲对象。存放矩形屏幕的 4 个物理坐标和 4 个贴图坐标
 * └── vao_: 顶点数组对象。与 VBO 数据绑定，负责解释其数据含义
 * 3. 纹理
 * └── texture_y/u/v_: 三个纹理通道
 */
class GLRenderer {
 public:
  enum class RenderMode {
    Normal,          // 默认（会变形）
    Stretch,         // 拉伸
    KeepAspectRatio  // 保持原比例（留黑边）
  };

  GLRenderer() = default;
  ~GLRenderer() { close(); }

  GLRenderer(const GLRenderer&) = delete;
  GLRenderer& operator=(const GLRenderer&) = delete;

  bool open(int width, int height, const char* title = "AVPlayer");
  void close();

  void render(const FramePtr& frame);

  void setRenderMode(RenderMode mode) { render_mode_ = mode; }
  GLFWwindow* getWindow() const { return window_; }

 private:
  bool initShaders();
  bool initTextures();
  void updateTextures(const AVFrame* frame);

  GLFWwindow* window_ = nullptr;
  GLuint shader_program_ = 0;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint texture_y_ = 0;
  GLuint texture_u_ = 0;
  GLuint texture_v_ = 0;

  int video_width_ = 0;
  int video_height_ = 0;
  RenderMode render_mode_ = RenderMode::KeepAspectRatio;

  // --- CPU 软解保底机制 ---
  SwsContextPtr sws_ctx_;     // 格式转换上下文
  FramePtr converted_frame_;  // 转换后的临时缓存帧

  static const char* VERTEX_SHADER_SOURCE;
  static const char* FRAGMENT_SHADER_SOURCE;
};

}  // namespace avplayer