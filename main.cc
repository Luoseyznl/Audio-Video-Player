#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include "src/player.h"

using namespace avplayer;

static std::atomic<bool> quit(false);
static Player* g_player = nullptr;

// 捕获 Ctrl+C 信号，优雅退出
static void signal_handler(int signum) {
  if (signum == SIGINT) {
    std::cout << "\nReceived SIGINT, stopping playback..." << std::endl;
    quit = true;
    if (g_player) {
      g_player->stop();
    }
  }
}

void printUsage(const char* programName) {
  std::cout << "Usage: " << programName << " <video_file>" << std::endl;
  std::cout << "\nControls:" << std::endl;
  std::cout << "  [Space] / [P] : Play / Pause" << std::endl;
  std::cout << "  [Left] / [Right]: Seek -10s / +10s" << std::endl;
  std::cout << "  [Up] / [Down] : Volume Up / Down" << std::endl;
  std::cout << "  [M]           : Mute / Unmute" << std::endl;
  std::cout << "  [R]           : Restart" << std::endl;
  std::cout << "  [S]           : Step forward (when paused)" << std::endl;
  std::cout << "  [Q] / [Esc]   : Quit" << std::endl;
}

// 键盘事件处理逻辑
void handleKeyPress(Player& player, int key) {
  switch (key) {
    case GLFW_KEY_SPACE:
    case GLFW_KEY_P:
      if (player.getState() == Player::State::Playing) {
        player.pause();
      } else {
        player.resume();
      }
      break;
    case GLFW_KEY_Q:
    case GLFW_KEY_ESCAPE:
      player.stop();
      quit = true;
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
        // 模拟单帧步进 (约 40ms)
        player.seek(player.getCurrentTimestamp() + 0.040);
      }
      break;
    case GLFW_KEY_M:
      if (player.getVolume() > 0.0) {
        player.setVolume(0.0);
      } else {
        player.setVolume(1.0);  // 恢复最大音量
      }
      break;
  }
}

// GLFW 键盘回调中转站
void keyCallback(GLFWwindow* window, int key, int scancode, int action,
                 int mods) {
  // 只响应按下事件，不响应长按的重复触发 (REPEAT) 和松开 (RELEASE)
  if (action != GLFW_PRESS) return;

  if (g_player) {
    handleKeyPress(*g_player, key);
  }
}

// 进度刷新回调 (UI 层)
void updateProgress(double currentTimeS, double durationS) {
  int hours = static_cast<int>(currentTimeS) / 3600;
  int minutes = (static_cast<int>(currentTimeS) / 60) % 60;
  int seconds = static_cast<int>(currentTimeS) % 60;

  int totalHours = static_cast<int>(durationS) / 3600;
  int totalMinutes = (static_cast<int>(durationS) / 60) % 60;
  int totalSeconds = static_cast<int>(durationS) % 60;

  char progress[128];
  snprintf(progress, sizeof(progress),
           "AVPlayer - %02d:%02d:%02d / %02d:%02d:%02d", hours, minutes,
           seconds, totalHours, totalMinutes, totalSeconds);

  // 1. 在终端同行覆盖打印进度
  printf("\r%s", progress);
  fflush(stdout);

  // 2. 实时更新图形窗口的标题！
  if (g_player && g_player->getWindow()) {
    glfwSetWindowTitle(g_player->getWindow(), progress);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printUsage(argv[0]);
    return 1;
  }

  // 注册 Ctrl+C 拦截
  signal(SIGINT, signal_handler);

  Player player;
  g_player = &player;

  std::cout << "Loading media: " << argv[1] << " ..." << std::endl;

  if (!player.open(argv[1])) {
    std::cout << "Failed to open media file!" << std::endl;
    return -1;
  }

  // 绑定交互和 UI 回调
  GLFWwindow* window = player.getWindow();
  if (window) {
    glfwSetKeyCallback(window, keyCallback);
  }
  player.setTimestampCallback(updateProgress);

  // 开始播放
  player.play();

  // ==========================================
  // 主事件轮询循环 (UI 线程的核心)
  // ==========================================
  while (!quit) {
    // 监听键盘鼠标、窗口拖动等操作系统级事件
    if (window) {
      glfwPollEvents();
      if (glfwWindowShouldClose(window)) {
        std::cout << "\nWindow closed by user." << std::endl;
        break;
      }
    }

    if (player.isFinished()) {
      std::cout << "\nPlayback completed." << std::endl;
      break;
    }

    // 防止主线程空转吃满 CPU
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  player.close();
  std::cout << "AVPlayer exited cleanly." << std::endl;
  g_player = nullptr;

  return 0;
}