#include "av_reader.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "logger.h"

namespace avplayer {

class AVReaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    utils::Logger::setGlobalLevel(utils::LogLevel::DEBUG);
  }

  const std::string test_file = "../../assets/LA.mp4";
};

// ==========================================
// 测试 1: 基础文件打开与流信息探测
// ==========================================
TEST_F(AVReaderTest, OpenAndCheckStreams) {
  AVReader reader;
  ASSERT_TRUE(reader.open(test_file)) << "Failed to open " << test_file;

  const AVStream* video_stream = reader.getVideoStream();
  EXPECT_NE(video_stream, nullptr);

  LOG_INFO << "File duration: " << reader.getDuration() << " us";
}

// ==========================================
// 测试 2: 核心多线程拉包流程
// ==========================================
TEST_F(AVReaderTest, PullPacketsFlow) {
  AVReader reader;
  ASSERT_TRUE(reader.open(test_file));

  reader.start();
  LOG_INFO << "AVReader started, pulling packets...";

  const AVStream* video_stream = reader.getVideoStream();
  ASSERT_NE(video_stream, nullptr);

  // 模拟消费者：拉取 20 个视频包
  int pulled_count = 0;
  for (int i = 0; i < 20; ++i) {
    PacketPtr pkt = reader.pullVideoPacket();

    if (!pkt) {
      LOG_INFO << "Received EOF signal (nullptr packet)";
      break;
    }

    EXPECT_EQ(pkt->stream_index, video_stream->index);
    EXPECT_GT(pkt->size, 0);  // 包里应该有数据
    pulled_count++;
  }

  EXPECT_EQ(pulled_count, 20);

  // 测试优雅退出
  reader.stop();
  LOG_INFO << "AVReader stopped cleanly";
}

// ==========================================
// 测试 3: 背压机制 (防内存爆满)
// ==========================================
TEST_F(AVReaderTest, BackpressureAndBlock) {
  AVReader reader(5, 5);
  ASSERT_TRUE(reader.open(test_file));

  reader.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  LOG_INFO << "Queues should be full and thread sleeping now.";

  PacketPtr pkt = reader.pullVideoPacket();
  EXPECT_NE(pkt, nullptr);

  reader.stop();
}

// ==========================================
// 测试 4: Seek 操作是否能正确清空队列并跳转
// ==========================================
TEST_F(AVReaderTest, SeekClearsQueue) {
  AVReader reader;
  ASSERT_TRUE(reader.open(test_file));
  reader.start();

  PacketPtr first_pkt = reader.pullVideoPacket();
  ASSERT_NE(first_pkt, nullptr);
  int64_t initial_pts = first_pkt->pts;

  int64_t target_ts = 5000000;
  EXPECT_TRUE(reader.seek(target_ts));

  PacketPtr seek_pkt = reader.pullVideoPacket();
  ASSERT_NE(seek_pkt, nullptr);

  EXPECT_GT(seek_pkt->pts, initial_pts);

  LOG_INFO << "Seek Test Passed. Initial PTS: " << initial_pts
           << " -> Seek PTS: " << seek_pkt->pts;

  reader.stop();
}

}  // namespace avplayer