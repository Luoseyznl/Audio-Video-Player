#pragma once

#include <string>

#include "mediadefs.h"

struct AVStream;  // 前向声明 FFmpeg 结构体，减少头文件污染

namespace avplayer {

class Demuxer {
 public:
  Demuxer() = default;
  ~Demuxer();

  Demuxer(const Demuxer&) = delete;
  Demuxer& operator=(const Demuxer&) = delete;

  bool open(const std::string& filename);  // 阻塞等待
  void close();                            // 幂等释放

  PacketPtr pullPacket();           // 阻塞取包
  bool seek(int64_t timestamp_us);  // 阻塞跳转后向最近时刻（us）

  AVStream* getAVStream(MediaType type) const;
  int64_t getDuration() const;  // 总时长（us）

  int getStreamIndex(MediaType type) const {
    if (type == MediaType::Video) return video_stream_index_;
    if (type == MediaType::Audio) return audio_stream_index_;
    return -1;
  }
  bool isEOF() const { return eof_; }

 private:
  FormatContextPtr format_ctx_;
  int video_stream_index_ = -1;
  int audio_stream_index_ = -1;
  bool eof_ = false;
};

}  // namespace avplayer
