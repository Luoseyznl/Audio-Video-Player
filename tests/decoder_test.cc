#include "decoder.h"

#include <gtest/gtest.h>

#include "demuxer.h"
#include "logger.h"

namespace avplayer {

class DecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    utils::Logger::setGlobalLevel(utils::LogLevel::DEBUG);

    ASSERT_TRUE(demuxer.open(test_file)) << "Failed to open " << test_file;
  }

  const std::string test_file = "../../assets/LA.mp4";
  Demuxer demuxer;
};

// ==========================================
// 测试 1: 视频解码器初始化
// ==========================================
TEST_F(DecoderTest, OpenVideoDecoder) {
  AVStream* video_stream = demuxer.getAVStream(MediaType::Video);
  ASSERT_NE(video_stream, nullptr) << "No video stream found in the file";

  Decoder video_decoder;
  bool success = video_decoder.open(video_stream);
  EXPECT_TRUE(success);
  EXPECT_TRUE(video_decoder.isOpen());

  auto info = video_decoder.getStreamInfo();
  EXPECT_EQ(info.type, MediaType::Video);
  EXPECT_GT(info.width, 0);
  EXPECT_GT(info.height, 0);
  EXPECT_NE(info.pixel_format, AV_PIX_FMT_NONE);

  LOG_INFO << "Video Test Passed. Resolution: " << info.width << "x"
           << info.height;
}

// ==========================================
// 测试 2: 音频解码器初始化
// ==========================================
TEST_F(DecoderTest, OpenAudioDecoder) {
  AVStream* audio_stream = demuxer.getAVStream(MediaType::Audio);
  if (!audio_stream) {
    LOG_WARN << "No audio stream found, skipping audio decoder test";
    return;
  }

  Decoder audio_decoder;
  bool success = audio_decoder.open(audio_stream);
  EXPECT_TRUE(success);
  EXPECT_TRUE(audio_decoder.isOpen());

  auto info = audio_decoder.getStreamInfo();
  EXPECT_EQ(info.type, MediaType::Audio);
  EXPECT_GT(info.sample_rate, 0);
  EXPECT_GT(info.channels, 0);
  EXPECT_NE(info.sample_format, AV_SAMPLE_FMT_NONE);
}

// ==========================================
// 测试 3: 核心流水线验证 (推包与拉帧)
// ==========================================
TEST_F(DecoderTest, DecodeVideoFramesPipeline) {
  AVStream* video_stream = demuxer.getAVStream(MediaType::Video);
  ASSERT_NE(video_stream, nullptr);
  int video_idx = demuxer.getStreamIndex(MediaType::Video);

  Decoder video_decoder;
  ASSERT_TRUE(video_decoder.open(video_stream));

  int decoded_frames_count = 0;
  const int target_frames = 5;  // 我们只测 5 帧，证明流水线跑通即可

  // 开始运转：Demuxer 拉包 -> Decoder 推包 -> Decoder 拉帧
  while (decoded_frames_count < target_frames) {
    PacketPtr pkt = demuxer.pullPacket();

    if (!pkt) {
      LOG_INFO << "EOF reached before decoding target number of frames";
      break;
    }

    if (pkt->stream_index == video_idx) {
      // 1. 推包 (Push)
      EXPECT_TRUE(video_decoder.pushPacket(pkt));

      // 2. 拉帧 (Pull) —— 注意：推 1 个包可能解出 0 帧，也可能解出多帧！
      while (true) {
        FramePtr frame = video_decoder.pullFrame();
        if (!frame) {
          break;
        }

        decoded_frames_count++;
        EXPECT_GT(frame->width, 0);
        EXPECT_GT(frame->height, 0);

        LOG_INFO << "Successfully decoded video frame " << decoded_frames_count
                 << " [PTS: " << frame->pts << "]";
      }
    }
  }

  EXPECT_GT(decoded_frames_count, 0);
}

// ==========================================
// 测试 4: Flush 机制测试
// ==========================================
TEST_F(DecoderTest, FlushDecoderAfterSeek) {
  AVStream* video_stream = demuxer.getAVStream(MediaType::Video);
  ASSERT_NE(video_stream, nullptr);

  Decoder video_decoder;
  ASSERT_TRUE(video_decoder.open(video_stream));

  bool frame_decoded = false;
  for (int i = 0; i < 30; ++i) {
    auto pkt = demuxer.pullPacket();
    if (pkt && pkt->stream_index == demuxer.getStreamIndex(MediaType::Video)) {
      video_decoder.pushPacket(pkt);
      if (video_decoder.pullFrame()) {
        frame_decoded = true;
      }
    }
  }
  EXPECT_TRUE(frame_decoded);

  EXPECT_TRUE(demuxer.seek(5000000));  // 跳转到 5 秒

  video_decoder.flush();
  LOG_INFO << "Decoder flushed successfully after seek";

  bool new_frame_decoded = false;
  for (int i = 0; i < 30; ++i) {
    auto pkt = demuxer.pullPacket();
    if (pkt && pkt->stream_index == demuxer.getStreamIndex(MediaType::Video)) {
      video_decoder.pushPacket(pkt);
      if (video_decoder.pullFrame()) {
        new_frame_decoded = true;
        break;  // 只要能重新解出一帧即证明成功
      }
    }
  }
  EXPECT_TRUE(new_frame_decoded);
}

}  // namespace avplayer