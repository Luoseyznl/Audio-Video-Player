#define GL_SILENCE_DEPRECATION

// 必须先包含 glew，再包含 glfw
#include "gl_renderer.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "logger.h"

using namespace utils;

namespace avplayer {

// ==========================================
// 1. 顶点着色器 (Vertex Shader)
// 职责：将视频画面映射到窗口矩形上
// ==========================================
const char* GLRenderer::VERTEX_SHADER_SOURCE = R"(
#version 330 core
layout (location = 0) in vec3 aPos;      // 输入：矩形的顶点坐标 (屏幕位置)
layout (location = 1) in vec2 aTexCoord; // 输入：纹理坐标 (对应视频画面的位置)

out vec2 TexCoord; // 输出：传给下一个阶段(片段着色器)的纹理坐标

void main() {
    // 把二维矩形放到三维空间中渲染，1.0 是齐次坐标
    gl_Position = vec4(aPos, 1.0);
    // 直接把输入的纹理坐标透传给片段着色器
    TexCoord = aTexCoord;
}
)";

// ==========================================
// 2. 片段着色器 (Fragment Shader) —— 核心！
// 职责：接收 Y/U/V 三张黑白图，合并计算出每个像素的真彩色 (RGB)
// ==========================================
const char* GLRenderer::FRAGMENT_SHADER_SOURCE = R"(
#version 330 core
out vec4 FragColor; // 最终输出给屏幕的颜色 (R, G, B, Alpha)

in vec2 TexCoord;   // 从顶点着色器接收的当前坐标

// 显存里的三个黑白纹理 (代表 Y, U, V)
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;

void main() {
    // 1. 获取亮度 (Y)。 .r 表示只取颜色值里的红色通道（因为这是张黑白单通道图）
    float y = texture(texY, TexCoord).r;
    
    // 2. 获取色度 (U, V)。
    // 为什么要减去 0.5？因为在计算机图形学里，纹理颜色值范围是 0.0 ~ 1.0。
    // 但数学公式里，U 和 V 的理论范围是 -0.5 ~ +0.5。所以必须减掉 0.5 做偏移回归。
    float u = texture(texU, TexCoord).r - 0.5;
    float v = texture(texV, TexCoord).r - 0.5;
    
    // 3. 经典的 YUV 转 RGB 矩阵乘法公式 (BT.601 标准)
    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;
    
    // 4. 将算好的 RGB 颜色填入最终像素 (1.0 代表不透明度 Alpha)
    FragColor = vec4(r, g, b, 1.0);
}
)";

GLRenderer::GLRenderer() {}

GLRenderer::~GLRenderer() { close(); }

bool GLRenderer::open(int width, int height, const char* title) {
  LOG_INFO << "Initializing GL renderer with " << width << "x" << height;

  video_width_ = width;
  video_height_ = height;

  if (!glfwInit()) {
    LOG_ERROR << "Failed to initialize GLFW";
    return false;
  }

  // 告诉 GLFW 我们需要 OpenGL 3.3 核心模式
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);  // Mac 必须加这句
#endif

  window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!window_) {
    LOG_ERROR << "Failed to create GLFW window";
    glfwTerminate();
    return false;
  }

  // 把当前窗口设置为 OpenGL 的主战场
  glfwMakeContextCurrent(window_);

  // 初始化 GLEW (必须在窗口创建之后)
  // 设置 glewExperimental 解决一些核心模式下的 bug
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    LOG_ERROR << "Failed to initialize GLEW";
    return false;
  }

  if (!initShaders()) return false;
  if (!initTextures()) return false;

  glViewport(0, 0, width, height);
  glfwMakeContextCurrent(nullptr);
  return true;
}

void GLRenderer::render(const FramePtr& frame) {
  if (!frame || !window_) return;

  glfwMakeContextCurrent(window_);

  int window_width, window_height;
  glfwGetFramebufferSize(window_, &window_width, &window_height);
  glViewport(0, 0, window_width, window_height);

  // 清空上一帧的画面，涂成黑色背景
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // 把内存里的 YUV 数据拷贝到显卡显存里
  updateTextures(frame.get());

  // 告诉 GPU 开始工作
  glUseProgram(shader_program_);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 6);  // 画 6 个顶点（刚好组成一个矩形）

  // 前后显存缓冲区交换，让画好的这一帧显示到屏幕上
  glfwSwapBuffers(window_);
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

  texture_y_ = texture_u_ = texture_v_ = vbo_ = vao_ = shader_program_ = 0;
}

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

bool GLRenderer::initTextures() {
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);

  // 定义矩形的 4 个顶点，分成 2 个三角形 (共 6 个点)
  float vertices[] = {
      // 屏幕坐标(x,y,z)        // 纹理坐标(u,v) - 注意：纹理坐标系原点不同
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

  // 告诉 GPU 如何解析上面的数组：前 3 个 float 是位置
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  // 后 2 个 float 是纹理坐标
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // 创建 Y、U、V 三个纹理通道
  glGenTextures(1, &texture_y_);
  glGenTextures(1, &texture_u_);
  glGenTextures(1, &texture_v_);

  // 配置纹理参数（过滤和环绕模式）
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

  // 绑定纹理槽位到对应的 Shader 变量
  glUseProgram(shader_program_);
  glUniform1i(glGetUniformLocation(shader_program_, "texY"),
              0);  // 对应 GL_TEXTURE0
  glUniform1i(glGetUniformLocation(shader_program_, "texU"),
              1);  // 对应 GL_TEXTURE1
  glUniform1i(glGetUniformLocation(shader_program_, "texV"),
              2);  // 对应 GL_TEXTURE2

  return true;
}

void GLRenderer::updateTextures(const AVFrame* frame) {
  if (frame->format != AV_PIX_FMT_YUV420P) {
    LOG_WARN << "GLRenderer currently only supports YUV420P";
    return;
  }

  // 传输 Y 分量 (全分辨率)
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_y_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frame->width, frame->height, 0, GL_RED,
               GL_UNSIGNED_BYTE, frame->data[0]);

  // 传输 U 分量 (宽高各一半)
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, texture_u_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frame->width / 2, frame->height / 2, 0,
               GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);

  // 传输 V 分量 (宽高各一半)
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, texture_v_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frame->width / 2, frame->height / 2, 0,
               GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);

  // 恢复默认状态，防止影响其他渲染
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

}  // namespace avplayer