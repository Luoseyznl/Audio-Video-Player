#pragma once

extern "C" {
#include <libavutil/pixfmt.h>  // 轻量级枚举头文件
#include <libavutil/samplefmt.h>
}

#include "mediadefs.h"

struct AVStream;
struct AVCodecContext;

namespace avplayer {

class Decoder {
 public:
  // 解码后的码流参数
  struct StreamInfo {
    MediaType type = MediaType::Unknown;

    int width = 0;
    int height = 0;
    AVPixelFormat pixel_format = AV_PIX_FMT_NONE;

    int sample_rate = 0;
    int channels = 0;
    AVSampleFormat sample_format = AV_SAMPLE_FMT_NONE;
  };

  Decoder() = default;
  ~Decoder();

  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  bool open(const AVStream* stream);  // 读取 stream->codecpar 并初始化解码器
  void close();

  void flush();  // 刷新解码器内部缓冲区

  bool pushPacket(const PacketPtr& pkt);  // Drain 模式
  FramePtr pullFrame();  // 从解码器拉取原始帧，完成后返回 FramePtr 或 nullptr

  bool isOpen() const { return codec_ctx_ != nullptr; }
  const StreamInfo& getStreamInfo() const { return info_; }

  // 仅暴露给强相关的下游组件 (如 SwsContext/SwrContext 转换器初始化时)
  AVCodecContext* getCodecContext() const { return codec_ctx_.get(); }

 private:
  CodecContextPtr codec_ctx_;
  StreamInfo info_;
};

}  // namespace avplayer