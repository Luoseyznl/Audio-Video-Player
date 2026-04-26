#include "demuxer.h"

#include <gtest/gtest.h>

#include "logger.h"

namespace avplayer {

class DemuxerTest : public ::testing::Test {
 protected:
  // 每个 TEST_F 运行前都会执行 SetUp
  void SetUp() override {
    // utils::Logger::setGlobalLevel(utils::LogLevel::DEBUG);
  }

  const std::string test_file = "../../assets/LA.mp4";
};

// ==========================================
// 1. 生命周期与基础信息测试
// ==========================================
TEST_F(DemuxerTest, OpensValidMediaFileAndGetsDuration) {
  Demuxer demuxer;

  // 使用 ASSERT_TRUE：如果打开失败，直接终止当前测试，不往下走
  ASSERT_TRUE(demuxer.open(test_file))
      << "Make sure the test asset exists: " << test_file;

  int64_t duration_us = demuxer.getDuration();
  LOG_INFO << "Duration: " << duration_us << " us";

  EXPECT_GT(duration_us, 0);
}

// ==========================================
// 2. 媒体轨道探测测试
// ==========================================
TEST_F(DemuxerTest, DiscoversAudioAndVideoStreams) {
  Demuxer demuxer;
  ASSERT_TRUE(demuxer.open(test_file));

  int video_idx = demuxer.getStreamIndex(MediaType::Video);
  int audio_idx = demuxer.getStreamIndex(MediaType::Audio);

  LOG_INFO << "Video Stream Index: " << video_idx;
  LOG_INFO << "Audio Stream Index: " << audio_idx;

  EXPECT_GE(video_idx, 0);
  EXPECT_NE(demuxer.getAVStream(MediaType::Video), nullptr);
}

// ==========================================
// 3. 生产提货 (取包) 循环测试
// ==========================================
TEST_F(DemuxerTest, PullsPacketsContinuously) {
  Demuxer demuxer;
  ASSERT_TRUE(demuxer.open(test_file));

  int packet_count = 0;
  const int target_pulls = 50;

  for (int i = 0; i < target_pulls; ++i) {
    PacketPtr pkt = demuxer.pullPacket();

    if (pkt) {
      packet_count++;
      EXPECT_TRUE(
          pkt->stream_index == demuxer.getStreamIndex(MediaType::Video) ||
          pkt->stream_index == demuxer.getStreamIndex(MediaType::Audio));
    } else if (demuxer.isEOF()) {
      LOG_INFO << "Reached EOF before pulling " << target_pulls << " packets.";
      break;
    } else {
      FAIL() << "Failed to pull packet, but not at EOF.";
    }
  }

  EXPECT_GT(packet_count, 0) << "Should have pulled at least one packet.";
  LOG_INFO << "Successfully read " << packet_count << " packets";
}

// ==========================================
// 4. 时空跳转测试
// ==========================================
TEST_F(DemuxerTest, SeeksToSpecificTimestamp) {
  Demuxer demuxer;
  ASSERT_TRUE(demuxer.open(test_file));

  // 跳转到第 2 秒 (2,000,000 微秒)
  int64_t target_us = 2000000;
  bool seek_success = demuxer.seek(target_us);
  ASSERT_TRUE(seek_success) << "Seek operation returned false.";

  // 跳转后读取一个包
  PacketPtr pkt = demuxer.pullPacket();
  ASSERT_NE(pkt, nullptr) << "Failed to pull packet immediately after seek.";

  LOG_INFO << "Seek to 2s succeeded. First packet stream_index: "
           << pkt->stream_index;
}

}  // namespace avplayer

// ==========================================
// GTest 自定义主函数
// ==========================================
int main(int argc, char** argv) {
  // 1. 初始化 GTest 框架 (解析命令行参数等)
  ::testing::InitGoogleTest(&argc, argv);

  // 2. 初始化全局依赖 (Logger)
  utils::LogConfig log_config;
  log_config.log_dir = "./test_logs";  // 测试日志单独放一个目录
  log_config.async_mode =
      false;  // 测试环境强烈建议用同步写入，防止测试崩溃时丢日志

  utils::Logger::init(log_config);
  utils::Logger::setGlobalLevel(utils::LogLevel::DEBUG);

  LOG_INFO << "Starting AVPlayer Demuxer Test Suite";

  // 3. 运行所有测试用例
  int ret = RUN_ALL_TESTS();

  LOG_INFO << "Test Suite Finished with code: " << ret;

  return ret;
}