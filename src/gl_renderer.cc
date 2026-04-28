#define GL_SILENCE_DEPRECATION

#include "gl_renderer.h"

#include "logger.h"

using namespace utils;

namespace avplayer {

// 顶点着色器
const char* GLRenderer::VERTEX_SHADER_SOURCE = R"(
#version 330 core
layout (location = 0) in vec3 aPos;      // 顶点画布坐标 (x, y, z)
layout (location = 1) in vec2 aTexCoord; // 顶点纹理坐标 (u, v)
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 1.0);       // 齐次坐标 (x, y, z, w)
    TexCoord = aTexCoord;  // 将纹理坐标传递给片段着色器进行线性插值
}
)";

// 片段着色器
const char* GLRenderer::FRAGMENT_SHADER_SOURCE = R"(
#version 330 core
out vec4 FragColor;     // 像素（片段）色彩
in vec2 TexCoord;       // 线性插值后的纹理坐标
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
void main() {
    // 1. 从 YUV 各个分量平面提取 TexCoord 位置的颜色分量
    float y = texture(texY, TexCoord).r;
    float u = texture(texU, TexCoord).r - 0.5;
    float v = texture(texV, TexCoord).r - 0.5;
    
    // 2. 使用 BT.601 转换公式进行色彩空间转换
    vec3 rgb;
    rgb.r = y + 1.402 * v;
    rgb.g = y - 0.344 * u - 0.714 * v;
    rgb.b = y + 1.772 * u;
    
    FragColor = vec4(rgb, 1.0); // Alpha 通道设为 1.0 (不透明)
}
)";

bool GLRenderer::open(int width, int height, const char* title) {
  video_width_ = width;
  video_height_ = height;

  // 1. 初始化 GLFW 窗口系统
  if (!glfwInit()) {
    LOG_ERROR << "Failed to initialize GLFW";
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);  // OpenGL 3.3 核心模式
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!window_) {
    LOG_ERROR << "Failed to create GLFW window";
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window_);

  glewExperimental = GL_TRUE;

  if (glewInit() != GLEW_OK) {
    LOG_ERROR << "Failed to initialize GLEW";
    return false;
  }

  // 2. 编译并链接 Shader（顶点着色器、片段着色器）
  if (!initShaders()) return false;

  // 3. 准备纹理对象和顶点缓冲
  if (!initTextures()) return false;

  glViewport(0, 0, width, height);  // 设置绘图视口
  glfwMakeContextCurrent(nullptr);  // 解绑，直到 render 时绑定
  return true;
}

void GLRenderer::render(const FramePtr& frame) {
  if (!frame || !window_) return;

  // 1. 以 window_ 作为窗口上下文
  glfwMakeContextCurrent(window_);

  int window_width, window_height;
  glfwGetFramebufferSize(window_, &window_width, &window_height);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // 清空画面
  glClear(GL_COLOR_BUFFER_BIT);

  // 2. 处理等比例缩放 (Aspect Ratio) 逻辑
  int view_x = 0, view_y = 0;
  int view_w = window_width, view_h = window_height;
  if (render_mode_ == RenderMode::KeepAspectRatio && video_width_ > 0 &&
      video_height_ > 0) {
    float window_aspect = static_cast<float>(window_width) / window_height;
    float video_aspect = static_cast<float>(video_width_) / video_height_;
    if (window_aspect > video_aspect) {
      // 左右留黑边
      view_w = window_height * video_aspect;
      view_x = (window_width - view_w) / 2;
    } else {
      // 上下留黑边
      view_h = window_width / video_aspect;
      view_y = (window_height - view_h) / 2;
    }
  }
  glViewport(view_x, view_y, view_w, view_h);  // view_x/y 起点，view_w/h 宽高
  updateTextures(frame.get());  // 把内存里的 YUV 数据上传到显卡的显存（纹理）中

  glUseProgram(shader_program_);     // (1) 指定着色器程序
  glBindVertexArray(vao_);           // (2) 绑定 VAO（顶点与纹理）
  glDrawArrays(GL_TRIANGLES, 0, 6);  // (3) 绘制两个三角形形成矩形
  glfwSwapBuffers(window_);          // (4) 双缓冲交换：展示画好的画面
}

void GLRenderer::close() {
  if (window_) {
    glfwMakeContextCurrent(window_);
  }
  if (texture_y_) glDeleteTextures(1, &texture_y_);
  if (texture_u_) glDeleteTextures(1, &texture_u_);
  if (texture_v_) glDeleteTextures(1, &texture_v_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (shader_program_) glDeleteProgram(shader_program_);

  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();

  sws_ctx_.reset();
  converted_frame_.reset();

  texture_y_ = texture_u_ = texture_v_ = vbo_ = vao_ = shader_program_ = 0;
}

// 编译源码、检查错误并链接成一个可执行的 GPU 着色器程序
bool GLRenderer::initShaders() {
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &VERTEX_SHADER_SOURCE, nullptr);
  glCompileShader(vertexShader);

  GLint success;
  GLchar infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
    LOG_ERROR << "Vertex shader compilation failed: " << infoLog;
    return false;
  }

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &FRAGMENT_SHADER_SOURCE, nullptr);
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
    LOG_ERROR << "Fragment shader compilation failed: " << infoLog;
    return false;
  }

  shader_program_ = glCreateProgram();
  glAttachShader(shader_program_, vertexShader);
  glAttachShader(shader_program_, fragmentShader);
  glLinkProgram(shader_program_);

  glGetProgramiv(shader_program_, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shader_program_, 512, nullptr, infoLog);
    LOG_ERROR << "Shader program linking failed: " << infoLog;
    return false;
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  return true;
}

// 初始化几何体与纹理容器
bool GLRenderer::initTextures() {
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);

  // 定义矩形的 6 个顶点：(x, y, z) + (u, v)
  float vertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  // 左下
      1.0f,  -1.0f, 0.0f, 1.0f, 1.0f,  // 右下
      -1.0f, 1.0f,  0.0f, 0.0f, 0.0f,  // 左上

      -1.0f, 1.0f,  0.0f, 0.0f, 0.0f,  // 左上
      1.0f,  -1.0f, 0.0f, 1.0f, 1.0f,  // 右下
      1.0f,  1.0f,  0.0f, 1.0f, 0.0f   // 右上
  };

  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  // 告诉 OpenGL 如何解析上面的数据 (0: 坐标, 1: 纹理坐标)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // 创建 3 个纹理 ID 用于存储 Y, U, V 平面数据
  glGenTextures(1, &texture_y_);
  glGenTextures(1, &texture_u_);
  glGenTextures(1, &texture_v_);

  auto configTexture = [](GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };

  configTexture(texture_y_);
  configTexture(texture_u_);
  configTexture(texture_v_);

  glUseProgram(shader_program_);
  glUniform1i(glGetUniformLocation(shader_program_, "texY"), 0);
  glUniform1i(glGetUniformLocation(shader_program_, "texU"), 1);
  glUniform1i(glGetUniformLocation(shader_program_, "texV"), 2);

  return true;
}

// 将 AVFrame 数据上传至显卡
void GLRenderer::updateTextures(const AVFrame* frame) {
  const AVFrame* target_frame = frame;

  // 如果格式不是 YUV420P，利用 libswscale 强制转换
  if (frame->format != AV_PIX_FMT_YUV420P) {
    sws_ctx_.reset(sws_getCachedContext(
        sws_ctx_.release(), frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!converted_frame_ || converted_frame_->width != frame->width ||
        converted_frame_->height != frame->height) {
      converted_frame_.reset(av_frame_alloc());
      converted_frame_->format = AV_PIX_FMT_YUV420P;
      converted_frame_->width = frame->width;
      converted_frame_->height = frame->height;
      av_frame_get_buffer(converted_frame_.get(), 0);
    }
    sws_scale(sws_ctx_.get(), frame->data, frame->linesize, 0, frame->height,
              converted_frame_->data, converted_frame_->linesize);
    target_frame = converted_frame_.get();
  }

  // 更新 Y 纹理 (Full size)
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_y_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, target_frame->width,
               target_frame->height, 0, GL_RED, GL_UNSIGNED_BYTE,
               target_frame->data[0]);

  // 更新 U 纹理 (1/2 size)
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, texture_u_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, target_frame->width / 2,
               target_frame->height / 2, 0, GL_RED, GL_UNSIGNED_BYTE,
               target_frame->data[1]);

  // 更新 V 纹理 (1/2 size)
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, texture_v_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[2]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, target_frame->width / 2,
               target_frame->height / 2, 0, GL_RED, GL_UNSIGNED_BYTE,
               target_frame->data[2]);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);  // 恢复设置
}

}  // namespace avplayer