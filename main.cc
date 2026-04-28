#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "logger.h"
#include "src/player.h"

using namespace avplayer;

static std::atomic<bool> g_quit{false};

static std::atomic<double> g_current_time_s{0.0};
static std::atomic<double> g_total_duration_s{0.0};

static void signal_handler(int signum) {
  if (signum == SIGINT) {
    std::cout << "\n[System] Received SIGINT, initiating graceful shutdown..."
              << std::endl;
    g_quit = true;  // Ctrl + C 时通过标志位实现优雅退出
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

std::string formatTime(double timeInSeconds) {
  int total_seconds = static_cast<int>(timeInSeconds);
  int hours = total_seconds / 3600;
  int minutes = (total_seconds / 60) % 60;
  int seconds = total_seconds % 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, minutes, seconds);
  return std::string(buf);
}

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
        player.seek(player.getCurrentTimestamp() + 0.04);  // 40ms 步进
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

  Player* player = static_cast<Player*>(glfwGetWindowUserPointer(window));
  if (player) {
    handleKeyPress(*player, key);
  }
}

int main(int argc, char* argv[]) {
  utils::LogConfig log_config;
  log_config.max_file_size = 10 * 1024 * 1024;
  log_config.max_files = 5;
  std::string target_dir = "/tmp/avplayer_logs";
  try {
    auto log_path = std::filesystem::temp_directory_path() / "avplayer_logs";
    if (!std::filesystem::exists(log_path))
      std::filesystem::create_directory(log_path);
    target_dir = log_path.string();
  } catch (...) { /* 保持默认 */
  }

  log_config.log_dir = target_dir;
  utils::Logger::init(log_config);
  utils::Logger::setGlobalLevel(utils::LogLevel::INFO);

  LOG_INFO << "Starting AVPlayer v1.0, LogDir: " << target_dir;

  if (argc != 2) {
    printUsage(argv[0]);
    return 1;
  }

  signal(SIGINT, signal_handler);  // 注册 Ctrl+C 处理函数

  Player player;

  if (!player.open(argv[1])) {
    LOG_FATAL << "Failed to open: " << argv[1];
    std::cerr << "Failed to open: " << argv[1] << std::endl;
    return -1;
  }

  LOG_INFO << "Successfully Open: " << argv[1];

  GLFWwindow* window = player.getWindow();
  if (window) {
    glfwSetWindowUserPointer(window, &player);  // 绑定指针
    glfwSetKeyCallback(window, keyCallback);    // 按键回调
  }

  // 在视频帧解码渲染线程里，通过回调取得当前时间和总时间（与用户看到的画面保持一致）
  player.setTimestampCallback([](double current_s, double duration_s) {
    g_current_time_s = current_s;
    g_total_duration_s = duration_s;
  });

  player.play();

  std::string last_title = "";
  while (!g_quit) {
    if (window) {
      glfwPollEvents();  // 主线程负责监听系统事件
      if (glfwWindowShouldClose(window)) {
        LOG_INFO << "Window closed by user";
        break;
      }
    }

    // 检查播放状态
    if (player.isFinished()) {
      break;
    }

    // 在时间标题变化时调用系统 API 进行更新（每秒一次）
    std::string new_title = "AVPlayer - " + formatTime(g_current_time_s) +
                            " / " + formatTime(g_total_duration_s);
    if (new_title != last_title) {
      //   printf("\r%s", new_title.c_str());
      //   fflush(stdout);

      if (window) {
        glfwSetWindowTitle(window, new_title.c_str());
      }
      last_title = new_title;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));  // 防止空转
  }

  player.close();
  LOG_INFO << "AVPlayer exited cleanly";

  return 0;
}