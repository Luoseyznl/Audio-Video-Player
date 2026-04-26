#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "logger.h"
#include "src/player.h"

using namespace avplayer;

// 仅用于接收 Ctrl+C 信号的原子标志位
static std::atomic<bool> g_quit{false};

// 用于主线程与解码线程安全同步时间的变量
static std::atomic<double> g_current_time_s{0.0};
static std::atomic<double> g_total_duration_s{0.0};

// 捕获 Ctrl+C 信号，优雅退出 (信号处理函数中只允许极简的赋值操作)
static void signal_handler(int signum) {
  if (signum == SIGINT) {
    std::cout << "\n[System] Received SIGINT, initiating graceful shutdown..."
              << std::endl;
    g_quit = true;
  }
}

void printUsage(const char* programName) {
  std::cout << "========================================" << std::endl;
  std::cout << " Usage: " << programName << " <video_file>" << std::endl;
  std::cout << "----------------------------------------" << std::endl;
  std::cout << " Controls:" << std::endl;
  std::cout << "  [Space] / [P] : Play / Pause" << std::endl;
  std::cout << "  [Left] / [Right]: Seek -10s / +10s" << std::endl;
  std::cout << "  [Up] / [Down] : Volume Up / Down" << std::endl;
  std::cout << "  [M]           : Mute / Unmute" << std::endl;
  std::cout << "  [R]           : Restart" << std::endl;
  std::cout << "  [S]           : Step forward (when paused)" << std::endl;
  std::cout << "  [Q] / [Esc]   : Quit" << std::endl;
  std::cout << "========================================" << std::endl;
}

// 格式化时间辅助函数 (将秒转换成 HH:MM:SS)
std::string formatTime(double timeInSeconds) {
  int total_seconds = static_cast<int>(timeInSeconds);
  int hours = total_seconds / 3600;
  int minutes = (total_seconds / 60) % 60;
  int seconds = total_seconds % 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, minutes, seconds);
  return std::string(buf);
}

// 键盘事件处理逻辑
void handleKeyPress(Player& player, int key) {
  switch (key) {
    case GLFW_KEY_SPACE:
    case GLFW_KEY_P:
      if (player.getState() == Player::State::Playing)
        player.pause();
      else
        player.resume();
      break;
    case GLFW_KEY_Q:
    case GLFW_KEY_ESCAPE:
      g_quit = true;
      break;
    case GLFW_KEY_R:
      player.seek(0.0);
      player.play();
      break;
    case GLFW_KEY_RIGHT:
      player.seek(player.getCurrentTimestamp() + 10.0);
      player.play();
      break;
    case GLFW_KEY_LEFT:
      player.seek(std::max(0.0, player.getCurrentTimestamp() - 10.0));
      player.play();
      break;
    case GLFW_KEY_UP:
      player.setVolume(std::min(1.0, player.getVolume() + 0.1));
      break;
    case GLFW_KEY_DOWN:
      player.setVolume(std::max(0.0, player.getVolume() - 0.1));
      break;
    case GLFW_KEY_S:
      if (player.getState() != Player::State::Playing) {
        player.seek(player.getCurrentTimestamp() + 0.040);  // 40ms 单帧步进
      }
      break;
    case GLFW_KEY_M:
      player.setVolume(player.getVolume() > 0.0 ? 0.0 : 1.0);
      break;
  }
}

// GLFW 键盘回调中转站
void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                 int mods) {
  if (action != GLFW_PRESS) return;

  // 提取我们之前绑定的 Player 指针，完美取代全局变量
  Player* player = static_cast<Player*>(glfwGetWindowUserPointer(window));
  if (player) {
    handleKeyPress(*player, key);
  }
}

int main(int argc, char* argv[]) {
  utils::LogConfig log_config;
  log_config.log_dir = "./avplayer_logs";
  log_config.max_file_size = 10 * 1024 * 1024;  // 10MB
  log_config.max_files = 5;
  log_config.async_mode = true;

  utils::Logger::init(log_config);
  utils::Logger::setGlobalLevel(utils::LogLevel::INFO);

  if (argc != 2) {
    printUsage(argv[0]);
    return 1;
  }

  // 注册 Ctrl+C 拦截
  signal(SIGINT, signal_handler);

  Player player;

  LOG_INFO << "Loading media: " << argv[1];

  if (!player.open(argv[1])) {
    LOG_FATAL << "Failed to open media file";
    std::cerr << "[Error] Failed to open media file!" << std::endl;
    return -1;
  }

  GLFWwindow* window = player.getWindow();
  if (window) {
    // 【核心优化】：将 player 对象与 window 绑定
    glfwSetWindowUserPointer(window, &player);
    glfwSetKeyCallback(window, keyCallback);
  }

  // 后台解码线程的回调只负责更新原子变量，绝不直接操作 UI
  player.setTimestampCallback([](double current_s, double duration_s) {
    g_current_time_s = current_s;
    g_total_duration_s = duration_s;
  });

  printUsage(argv[0]);
  player.play();

  // ==========================================
  // 主事件轮询循环 (UI 线程的核心)
  // ==========================================
  std::string last_title = "";

  while (!g_quit) {
    // 1. 监听操作系统级事件 (必须在主线程执行)
    if (window) {
      glfwPollEvents();
      if (glfwWindowShouldClose(window)) {
        LOG_INFO << "Window closed by user";
        break;
      }
    }

    // 2. 检查播放状态
    if (player.isFinished()) {
      break;
    }

    // 3. 安全更新 UI 状态 (将 GLFW 操作移回主线程)
    std::string new_title = "AVPlayer - " + formatTime(g_current_time_s) +
                            " / " + formatTime(g_total_duration_s);

    // 只有当标题发生变化时（每秒更新一次）才调用系统 API，节省性能
    if (new_title != last_title) {
      printf("\r%s", new_title.c_str());
      fflush(stdout);

      if (window) {
        glfwSetWindowTitle(window, new_title.c_str());
      }
      last_title = new_title;
    }

    // 防止主线程空转
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // 【终端换行保护】：防止退出时 shell 提示符覆盖进度条
  printf("\n");

  LOG_INFO << "Initiating player shutdown...";
  player.close();

  std::cout << "[System] AVPlayer exited cleanly." << std::endl;
  LOG_INFO << "AVPlayer exited cleanly.";

  return 0;
}