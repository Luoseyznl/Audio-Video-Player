#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include <string>

#include "mediadefs.h"

namespace avplayer {

/**
 * @brief 解封装器 (Demuxer)
 * 职责：打开多媒体文件（集装箱），读取出压缩的数据包（AVPacket）交由后续的解码器处理。
 */
class Demuxer {
 public:
  Demuxer() = default;
  ~Demuxer();

  bool open(const std::string& filename);
  void close();

  int getStreamIndex(MediaType type) const;
  AVStream* getAVStream(MediaType type) const;
  PacketPtr readPacket();

  // 跳转：flag = 0（最近关键帧）、AVSEEK_FLAG_BACKWARD（向后最近关键帧）
  bool seek(int64_t timestamp, int flags = 0);

  int64_t getDuration() const;
  bool isEOF() const { return eof_; }

 private:
  FormatContextPtr format_ctx_;
  int video_stream_index_ = -1;
  int audio_stream_index_ = -1;
  bool eof_ = false;
};

}  // namespace avplayer