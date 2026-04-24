#include "gl_renderer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "av_reader.h"
#include "decoder.h"
#include "logger.h"

namespace avplayer {

class GLRendererTest : public ::testing::Test {
 protected:
  void SetUp() override {
    utils::Logger::setGlobalLevel(utils::LogLevel::INFO);
  }
  const std::string test_file = "../../assets/LA.mp4";
};

TEST_F(GLRendererTest, PlayVideoContent) {
  AVReader reader;
  ASSERT_TRUE(reader.open(test_file));
  reader.start();

  const AVStream* video_stream = reader.getVideoStream();
  ASSERT_NE(video_stream, nullptr);

  Decoder video_decoder;
  ASSERT_TRUE(video_decoder.open(video_stream));

  auto info = video_decoder.getStreamInfo();
  LOG_INFO << "Video resolution: " << info.width << "x" << info.height;

  // 启动图形渲染窗口 (Renderer)
  GLRenderer renderer;
  ASSERT_TRUE(renderer.open(info.width, info.height, "AVPlayer - Video Test"));

  LOG_INFO << "Starting video playback test (approx 3 seconds)...";

  int frame_count = 0;
  const int max_frames_to_play = 100;  // 测试播放 100 帧自动结束

  while (frame_count < max_frames_to_play) {
    // 监听操作系统的窗口事件
    glfwPollEvents();
    if (glfwWindowShouldClose(renderer.getWindow())) {
      LOG_INFO << "Window closed by user.";
      break;
    }

    PacketPtr pkt = reader.pullVideoPacket();
    if (!pkt) {
      if (reader.isEOF()) break;
      continue;
    }

    if (video_decoder.pushPacket(pkt)) {
      while (true) {
        FramePtr frame = video_decoder.pullFrame();
        if (!frame) break;  // 需要更多包

        renderer.render(frame);
        frame_count++;

        // 休眠 33 毫秒 (大约模拟 30fps)
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
      }
    }
  }

  LOG_INFO << "Rendered " << frame_count << " frames. Test finished.";

  // 优雅退出
  reader.stop();
  renderer.close();
}

}  // namespace avplayer