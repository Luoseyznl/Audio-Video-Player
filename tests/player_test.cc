#include "player.h"

#include <GLFW/glfw3.h>  // 【新增】：为了使用 glfwPollEvents
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "logger.h"

namespace avplayer {

class PlayerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    utils::Logger::setGlobalLevel(utils::LogLevel::INFO);
  }
  const std::string test_file = "../../assets/LA.mp4";
};

TEST_F(PlayerTest, FullPlaybackLifecycle) {
  Player player;

  LOG_INFO << ">>> Action: Open File";
  ASSERT_TRUE(player.open(test_file));
  LOG_INFO << "Video duration: " << player.getDuration() << " seconds";

  player.setTimestampCallback([&](double ts_s, double duration_s) {
    static int last_printed_sec = -1;
    int current_sec = static_cast<int>(ts_s);
    if (current_sec != last_printed_sec) {
      LOG_INFO << "[UI ProgressBar] Progress: " << current_sec << "s / "
               << static_cast<int>(duration_s) << "s";
      last_printed_sec = current_sec;
    }
  });

  player.setStateCallback([](Player::State state) {
    const char* state_str[] = {"Stopped", "Playing", "Paused", "Error"};
    LOG_INFO << "[UI State] Player state changed to: "
             << state_str[static_cast<int>(state)];
  });

  // ==========================================
  // 【核心修复 3：智能带氧休眠函数】
  // 替代单纯的 sleep_for，保持对操作系统的事件响应，窗口再也不会假死！
  // ==========================================
  auto wait_and_pump = [&](int seconds) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <
           std::chrono::seconds(seconds)) {
      if (player.getWindow()) {
        glfwPollEvents();  // 不断告诉系统：我还活着，别把我标记为停止响应
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  };

  LOG_INFO << ">>> Action: Play";
  player.play();
  EXPECT_EQ(player.getState(), Player::State::Playing);

  wait_and_pump(4);  // 播放 4 秒

  LOG_INFO << ">>> Action: Pause";
  player.pause();
  EXPECT_EQ(player.getState(), Player::State::Paused);

  wait_and_pump(2);  // 暂停 2 秒，此时拖动窗口也不会卡死了！

  LOG_INFO << ">>> Action: Seek to 10.0s";
  EXPECT_TRUE(player.seek(10.0));

  LOG_INFO << ">>> Action: Set Volume to 50% and Resume";
  player.setVolume(0.5);
  player.resume();
  EXPECT_EQ(player.getState(), Player::State::Playing);

  wait_and_pump(4);  // 跳转后再看 4 秒

  LOG_INFO << ">>> Action: Stop";
  player.stop();
  EXPECT_EQ(player.getState(), Player::State::Stopped);
}

}  // namespace avplayer