#include "demuxer.h"

#include <gtest/gtest.h>

#include "logger.h"

namespace avplayer {

class DemuxerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    utils::Logger::setGlobalLevel(utils::LogLevel::DEBUG);
  }

  const std::string test_file = "../../assets/LA.mp4";
};

TEST_F(DemuxerTest, OpenFile) {
  Demuxer demuxer;
  bool success = demuxer.open(test_file);
  EXPECT_TRUE(success);

  if (success) {
    LOG_INFO << "Duration: " << demuxer.getDuration() << " us";
    EXPECT_GT(demuxer.getDuration(), 0);
  }
}

TEST_F(DemuxerTest, StreamDiscovery) {
  Demuxer demuxer;
  ASSERT_TRUE(demuxer.open(test_file));

  int video_idx = demuxer.getStreamIndex(MediaType::Video);
  int audio_idx = demuxer.getStreamIndex(MediaType::Audio);

  LOG_INFO << "Video Index: " << video_idx << ", Audio Index: " << audio_idx;

  EXPECT_GE(video_idx, 0);
  EXPECT_NE(demuxer.getAVStream(MediaType::Video), nullptr);
}

TEST_F(DemuxerTest, ReadPackets) {
  Demuxer demuxer;
  ASSERT_TRUE(demuxer.open(test_file));

  int packet_count = 0;
  for (int i = 0; i < 50; ++i) {
    PacketPtr pkt = demuxer.pullPacket();
    if (pkt) {
      packet_count++;
      EXPECT_TRUE(
          pkt->stream_index == demuxer.getStreamIndex(MediaType::Video) ||
          pkt->stream_index == demuxer.getStreamIndex(MediaType::Audio));
    } else if (demuxer.isEOF()) {
      break;
    }
  }
  EXPECT_GT(packet_count, 0);
  LOG_INFO << "Successfully read " << packet_count << " packets";
}

TEST_F(DemuxerTest, SeekTest) {
  Demuxer demuxer;
  ASSERT_TRUE(demuxer.open(test_file));

  // 跳转到第 2 秒 (2,000,000 微秒)
  int64_t target_us = 2000000;
  bool seek_success = demuxer.seek(target_us);
  EXPECT_TRUE(seek_success);

  // 跳转后读取一个包，验证其有效性
  PacketPtr pkt = demuxer.pullPacket();
  EXPECT_NE(pkt, nullptr);
}

}  // namespace avplayer