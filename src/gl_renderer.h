#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>  // GLFW 提供跨平台的窗口创建和 OpenGL 上下文管理

#include "mediadefs.h"

namespace avplayer {

/**
 * @brief OpenGL 视频渲染器
 * 职责：
 * 1. 创建并管理播放窗口。
 * 2. 接收 YUV 格式的 AVFrame，利用 GPU (Shader) 转换为 RGB 并渲染到屏幕。
 */
class GLRenderer {
 public:
  enum class RenderMode {
    Normal,          // 默认填充（可能变形）
    Stretch,         // 强制拉伸铺满
    KeepAspectRatio  // 保持原比例（留黑边，推荐）
  };

  GLRenderer();
  ~GLRenderer();

  bool open(int width, int height, const char* title = "AVPlayer");

  void close();

  void render(const FramePtr& frame);  // 只读引用，渲染器不负责释放 frame

  void setRenderMode(RenderMode mode) { render_mode_ = mode; }
  GLFWwindow* getWindow() const { return window_; }

 private:
  bool initShaders();
  bool initTextures();

  void updateTextures(const AVFrame* frame);  // 将 AVFrame 的数据搬运到显存中

  GLFWwindow* window_ = nullptr;

  // OpenGL 核心句柄 (GLuint)
  GLuint shader_program_ = 0;
  GLuint vao_ = 0;
  GLuint vbo_ = 0;

  // YUV420P 需要的三个纹理通道
  GLuint texture_y_ = 0;
  GLuint texture_u_ = 0;
  GLuint texture_v_ = 0;

  int video_width_ = 0;
  int video_height_ = 0;
  RenderMode render_mode_ = RenderMode::KeepAspectRatio;

  // GPU 着色器源码：负责顶点映射和 YUV->RGB 像素转换
  static const char* VERTEX_SHADER_SOURCE;
  static const char* FRAGMENT_SHADER_SOURCE;
};

}  // namespace avplayer