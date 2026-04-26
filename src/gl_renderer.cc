#define GL_SILENCE_DEPRECATION

#include "gl_renderer.h"

#include "logger.h"

using namespace utils;

namespace avplayer {

/**
 * 顶点着色器
 * 将画布位置(x, y, z)和片段位置(u, v)传递给 gl_Position 和 TexCoord
 */

const char* GLRenderer::VERTEX_SHADER_SOURCE = R"(
#version 330 core
layout (location = 0) in vec3 aPos;      // (x, y, z)
layout (location = 1) in vec2 aTexCoord; // (u, v)
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 1.0);       // (x, y, z, w)
    TexCoord = aTexCoord;
}
)";

/**
 * 片段着色器
 * 将 TexCoord 传给二维纹理采样器得到 YUV 纹理值，并转换为片段像素值 FragColor
 */
const char* GLRenderer::FRAGMENT_SHADER_SOURCE = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
void main() {
    float y = texture(texY, TexCoord).r;
    float u = texture(texU, TexCoord).r - 0.5;
    float v = texture(texV, TexCoord).r - 0.5;
    
    vec3 rgb;
    rgb.r = y + 1.402 * v;
    rgb.g = y - 0.344 * u - 0.714 * v;
    rgb.b = y + 1.772 * u;
    
    FragColor = vec4(rgb, 1.0);
}
)";

bool GLRenderer::open(int width, int height, const char* title) {
  LOG_INFO << "Initializing GL renderer with " << width << "x" << height;

  video_width_ = width;
  video_height_ = height;

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

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // 清空上一帧的画面（涂成黑色）
  glClear(GL_COLOR_BUFFER_BIT);

  int view_x = 0, view_y = 0;
  int view_w = window_width, view_h = window_height;

  if (render_mode_ == RenderMode::KeepAspectRatio && video_width_ > 0 &&
      video_height_ > 0) {
    float window_aspect = static_cast<float>(window_width) / window_height;
    float video_aspect = static_cast<float>(video_width_) / video_height_;

    if (window_aspect > video_aspect) {
      view_w = window_height * video_aspect;
      view_x = (window_width - view_w) / 2;
    } else {
      view_h = window_width / video_aspect;
      view_y = (window_height - view_h) / 2;
    }
  }

  glViewport(view_x, view_y, view_w, view_h);

  updateTextures(frame.get());  // 将 YUV 数据更新到显卡显存里

  glUseProgram(shader_program_);
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, 6);

  glfwSwapBuffers(window_);  // 换入显存缓冲区
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

  float vertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  // 1
      1.0f,  -1.0f, 0.0f, 1.0f, 1.0f,  // 2
      -1.0f, 1.0f,  0.0f, 0.0f, 0.0f,  // 3
      -1.0f, 1.0f,  0.0f, 0.0f, 0.0f,  // 3
      1.0f,  -1.0f, 0.0f, 1.0f, 1.0f,  // 2
      1.0f,  1.0f,  0.0f, 1.0f, 0.0f   // 4
  };

  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

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

void GLRenderer::updateTextures(const AVFrame* frame) {
  const AVFrame* target_frame = frame;

  // 软解保底机制：如果不是 YUV420P，动用 CPU 强转
  if (frame->format != AV_PIX_FMT_YUV420P) {
    // 1. 获取并缓存转换上下文 (sws_getCachedContext 会自动复用内存，非常高效)
    sws_ctx_.reset(sws_getCachedContext(
        sws_ctx_.release(), frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr));

    // 2. 如果缓存帧不存在或分辨率变了，重新分配内存
    if (!converted_frame_ || converted_frame_->width != frame->width ||
        converted_frame_->height != frame->height) {
      converted_frame_.reset(av_frame_alloc());
      converted_frame_->format = AV_PIX_FMT_YUV420P;
      converted_frame_->width = frame->width;
      converted_frame_->height = frame->height;
      av_frame_get_buffer(converted_frame_.get(), 0);
    }

    // 3. 执行转换
    sws_scale(sws_ctx_.get(), frame->data, frame->linesize, 0, frame->height,
              converted_frame_->data, converted_frame_->linesize);

    // 4. 将目标指针指向转换后的帧
    target_frame = converted_frame_.get();
  }

  // 此时 target_frame 是 YUV420P 格式
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_y_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, target_frame->width,
               target_frame->height, 0, GL_RED, GL_UNSIGNED_BYTE,
               target_frame->data[0]);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, texture_u_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, target_frame->width / 2,
               target_frame->height / 2, 0, GL_RED, GL_UNSIGNED_BYTE,
               target_frame->data[1]);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, texture_v_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[2]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, target_frame->width / 2,
               target_frame->height / 2, 0, GL_RED, GL_UNSIGNED_BYTE,
               target_frame->data[2]);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

}  // namespace avplayer