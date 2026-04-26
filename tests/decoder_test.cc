#include "decoder.h"

#include <gtest/gtest.h>

#include "logger.h"
#include "packet_producer.h"

namespace avplayer {

class DecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(producer.open(test_file))
        << "Failed to open test asset: " << test_file;
    // 【修复 1】：必须启动底层异步分发线程！
    producer.start();
  }

  void TearDown() override {
    // 【修复 1 附属】：优雅停止后台线程
    producer.stop();
    producer.close();
  }

  const std::string test_file = "../../assets/LA.mp4";
  PacketProducer producer;
};

// ==========================================
// 测试 1: 视频解码器初始化 (无需变动)
// ==========================================
TEST_F(DecoderTest, OpensVideoDecoderSuccessfully) {
  const AVStream* video_stream = producer.getVideoStream();
  ASSERT_NE(video_stream, nullptr) << "No video stream found in the file";

  Decoder video_decoder;
  ASSERT_TRUE(video_decoder.open(video_stream))
      << "Failed to open video decoder";
  EXPECT_TRUE(video_decoder.isOpen());

  auto info = video_decoder.getStreamInfo();
  EXPECT_EQ(info.type, MediaType::Video);
  EXPECT_GT(info.width, 0);
  EXPECT_GT(info.height, 0);
  EXPECT_NE(info.pixel_format, AV_PIX_FMT_NONE);

  LOG_INFO << "Video Decoder initialized. Resolution: " << info.width << "x"
           << info.height;
}

// ==========================================
// 测试 2: 音频解码器初始化 (无需变动)
// ==========================================
TEST_F(DecoderTest, OpensAudioDecoderSuccessfully) {
  const AVStream* audio_stream = producer.getAudioStream();
  if (!audio_stream) {
    LOG_WARN << "No audio stream found, skipping audio decoder test";
    return;
  }

  Decoder audio_decoder;
  ASSERT_TRUE(audio_decoder.open(audio_stream))
      << "Failed to open audio decoder";
  EXPECT_TRUE(audio_decoder.isOpen());

  auto info = audio_decoder.getStreamInfo();
  EXPECT_EQ(info.type, MediaType::Audio);
  EXPECT_GT(info.sample_rate, 0);
  EXPECT_GT(info.channels, 0);
  EXPECT_NE(info.sample_format, AV_SAMPLE_FMT_NONE);

  LOG_INFO << "Audio Decoder initialized. Sample Rate: " << info.sample_rate
           << "Hz";
}

// ==========================================
// 测试 3: 核心流水线验证 (推包与拉帧)
// ==========================================
TEST_F(DecoderTest, DecodesVideoFramesPipeline) {
  const AVStream* video_stream = producer.getVideoStream();
  ASSERT_NE(video_stream, nullptr);

  Decoder video_decoder;
  ASSERT_TRUE(video_decoder.open(video_stream));

  int decoded_frames_count = 0;
  const int target_frames = 5;

  while (decoded_frames_count < target_frames) {
    // 【修复 2】：直接拉取分拣好的 VideoPacket
    PacketPtr pkt = producer.pullVideoPacket();

    if (!pkt) {
      if (producer.isEOF()) {
        LOG_INFO << "EOF reached before decoding target number of frames";
      }
      break;
    }

    // 【修复 3】：省去了恶心的 stream_index 判断！直接推！
    // 1. 推包 (Push)
    EXPECT_TRUE(video_decoder.pushPacket(pkt));

    // 2. 拉帧 (Pull)
    while (true) {
      FramePtr frame = video_decoder.pullFrame();
      if (!frame) {
        break;  // 肚子空了，需要下一个 Packet 触发
      }

      decoded_frames_count++;
      EXPECT_GT(frame->width, 0);
      EXPECT_GT(frame->height, 0);

      LOG_INFO << "Successfully decoded video frame " << decoded_frames_count
               << " [PTS: " << frame->pts << "]";
    }
  }

  EXPECT_EQ(decoded_frames_count, target_frames)
      << "Failed to decode the target number of frames";
}

// ==========================================
// 测试 4: Flush 机制测试 (Seek 后的必备操作)
// ==========================================
TEST_F(DecoderTest, FlushesDecoderAfterSeek) {
  const AVStream* video_stream = producer.getVideoStream();
  ASSERT_NE(video_stream, nullptr);

  Decoder video_decoder;
  ASSERT_TRUE(video_decoder.open(video_stream));

  // 1. 先正常解码一些帧
  bool frame_decoded = false;
  for (int i = 0; i < 50; ++i) {
    // 【修复】：直接拉取分拣好的 VideoPacket
    auto pkt = producer.pullVideoPacket();
    if (pkt) {
      video_decoder.pushPacket(pkt);
      if (video_decoder.pullFrame()) {
        frame_decoded = true;
      }
    }
  }
  ASSERT_TRUE(frame_decoded) << "Initial decoding failed before seek";

  // 2. 发生 Seek 跳转
  ASSERT_TRUE(producer.seek(5000000));

  // 3. 执行 Flush 操作
  video_decoder.flush();
  LOG_INFO << "Decoder flushed successfully after seek";

  // 4. 验证 Flush 后能否从新的关键帧起步
  bool new_frame_decoded = false;
  for (int i = 0; i < 50; ++i) {
    // 【修复】：直接拉取分拣好的 VideoPacket
    auto pkt = producer.pullVideoPacket();
    if (pkt) {
      video_decoder.pushPacket(pkt);
      if (video_decoder.pullFrame()) {
        new_frame_decoded = true;
        break;
      }
    }
  }
  EXPECT_TRUE(new_frame_decoded)
      << "Failed to decode new frames after flushing";
}

}  // namespace avplayer
