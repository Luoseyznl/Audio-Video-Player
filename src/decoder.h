#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "mediadefs.h"

namespace avplayer {

class Decoder {
 public:
  struct StreamInfo {
    MediaType type = MediaType::Unknown;

    // 视频参数
    int width = 0;
    int height = 0;
    AVPixelFormat pixel_format = AV_PIX_FMT_NONE;

    // 音频参数
    int sample_rate = 0;
    int channels = 0;
    AVSampleFormat sample_format = AV_SAMPLE_FMT_NONE;
  };

  Decoder() = default;
  ~Decoder();

  bool open(const AVStream* stream);
  void close();

  void flush();  // 刷新解码器内部缓冲区

  bool pushPacket(const PacketPtr& pkt);  // 传入 nullptr 代表 EOF
  FramePtr pullFrame();  // 返回 nullptr 代表需要继续 pushPacket

  const StreamInfo& getStreamInfo() const { return info_; }
  AVCodecContext* getCodecContext() const { return codec_ctx_.get(); }
  bool isOpen() const { return codec_ctx_ != nullptr; }

 private:
  CodecContextPtr codec_ctx_;
  StreamInfo info_;
};

}  // namespace avplayer