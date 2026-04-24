#include "audio_player.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "av_reader.h"
#include "decoder.h"
#include "logger.h"

namespace avplayer {

class AudioPlayerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    utils::Logger::setGlobalLevel(utils::LogLevel::INFO);
  }

  const std::string test_file = "../../assets/LA.mp4";
};

TEST_F(AudioPlayerTest, PlayAudioContent) {
  // 1. 启动读包线程
  AVReader reader;
  ASSERT_TRUE(reader.open(test_file));
  reader.start();

  const AVStream* audio_stream = reader.getAudioStream();
  if (!audio_stream) {
    LOG_WARN << "No audio stream found in " << test_file << ". Skipping test.";
    return;
  }

  // ==========================================
  // 【核心修复】：启动“视频包排污”影子线程
  // ==========================================
  std::atomic<bool> drain_running{true};
  std::thread video_drainer([&]() {
    while (drain_running) {
      // 不断拉取视频包并任其销毁。
      // PacketPtr 离开作用域会自动触发 av_packet_free，不会内存泄漏。
      PacketPtr pkt = reader.pullVideoPacket();

      // 如果拿到 nullptr（比如 reader 被 stop 了），跳过即可
      if (!pkt && !drain_running) {
        break;
      }
    }
  });

  // 2. 启动音频解码器
  Decoder audio_decoder;
  ASSERT_TRUE(audio_decoder.open(audio_stream));

  // 3. 启动音频播放器 (声卡硬件)
  AudioPlayer player;
  ASSERT_TRUE(player.open(audio_decoder.getStreamInfo()));

  // 开始让声卡硬件去回调拉取数据
  player.play();

  LOG_INFO << "Starting audio playback test (approx 5 seconds)...";
  LOG_INFO << "Turn on your speakers/headphones!";

  // 4. 播放循环：依靠 getAudioClock() 判断是否播够了 5 秒 (5,000,000 微秒)
  const int64_t target_play_time_us = 5000000;

  while (player.getAudioClock() < target_play_time_us) {
    // 从队列拉取音频压缩包
    PacketPtr pkt = reader.pullAudioPacket();
    if (!pkt) {
      if (reader.isEOF()) break;
      continue;
    }

    // 将包推入解码器
    if (audio_decoder.pushPacket(pkt)) {
      while (true) {
        // 拉取解压后的 PCM 原始帧
        FramePtr frame = audio_decoder.pullFrame();
        if (!frame) break;

        // 将 PCM 数据推入音频播放器的缓冲队列
        // 注意：如果队列满了，这里会自动阻塞，从而天然地控制了解码速度
        player.enqueueFrame(std::move(frame));
      }
    }
  }

  LOG_INFO << "Audio test finished. Final audio clock: "
           << player.getAudioClock() / 1000000.0 << " seconds.";

  // ==========================================
  // 5. 优雅退出流程 (退出顺序极其关键！)
  // ==========================================

  // 第一步：停止播放器硬件回调
  player.stop();

  // 第二步：必须先停止 reader！
  // reader.stop() 内部会调用 video_queue_.stop()，这会让挂在 pullVideoPacket
  // 上的影子线程解除阻塞并返回 nullptr
  reader.stop();

  // 第三步：通知影子线程退出并回收
  drain_running = false;
  if (video_drainer.joinable()) {
    video_drainer.join();
  }
}

}  // namespace avplayer