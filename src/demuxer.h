#pragma once

extern "C" {
#include <libavformat/avformat.h>
}

#include <string>

#include "mediadefs.h"

namespace avplayer {

/**
 * @brief 解封装器 (Demuxer)
 * 职责：打开多媒体文件（集装箱），提取出压缩的数据包（AVPacket）交由后续的解码器处理。
 */
class Demuxer {
 public:
  Demuxer() = default;
  ~Demuxer();

  bool open(const std::string& filename);
  void close();

  int getStreamIndex(MediaType type) const;
  AVStream* getAVStream(MediaType type) const;

  /**
   * @brief 从媒体文件中拉取下一个压缩数据包
   * @return 包含压缩数据的智能指针。若读取完毕(EOF)或出错，返回 nullptr。
   */
  PacketPtr pullPacket();

  // 跳转到目标时间点之前最近的关键帧，再交给解码线程追到精确时间点
  bool seek(int64_t timestamp, int flags = AVSEEK_FLAG_BACKWARD);

  int64_t getDuration() const;
  bool isEOF() const { return eof_; }

 private:
  FormatContextPtr format_ctx_;
  int video_stream_index_ = -1;
  int audio_stream_index_ = -1;
  bool eof_ = false;
};

}  // namespace avplayer
