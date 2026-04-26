#include "demuxer.h"

#include "logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>  // av_rescale_q
}

using namespace utils;

namespace avplayer {

Demuxer::~Demuxer() {
  LOG_INFO << "Demuxer Destroying";
  close();
}

bool Demuxer::open(const std::string& filename) {
  LOG_INFO << "Demuxer opening media file: " << filename;

  // 1. 获取格式上下文
  AVFormatContext* raw_ctx = nullptr;
  int ret = avformat_open_input(&raw_ctx, filename.c_str(), nullptr, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Failed to open " << filename << ". Reason: " << errbuf;
    return false;
  }
  format_ctx_.reset(raw_ctx);

  // 2. 探测码流信息
  ret = avformat_find_stream_info(format_ctx_.get(), nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Failed to find stream information. Reason: " << errbuf;
    close();
    return false;
  }

  LOG_INFO << "Duration: " << format_ctx_->duration / AV_TIME_BASE << "s, "
           << "Streams: " << format_ctx_->nb_streams;

  // 3. 设置码流索引
  video_stream_index_ = av_find_best_stream(
      format_ctx_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  audio_stream_index_ = av_find_best_stream(
      format_ctx_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

  LOG_INFO << "Video stream at index: " << video_stream_index_
           << ", Audio stream at index: " << audio_stream_index_;

  return true;
}

void Demuxer::close() {
  if (format_ctx_) {
    LOG_INFO << "Closing demuxer...";
    format_ctx_.reset();  // 显式释放格式上下文（允许 Demuxer 复用）
  }

  video_stream_index_ = -1;
  audio_stream_index_ = -1;
  eof_ = false;
}

// AVStream 的生命周期与 AVFormatContext 绑定，不必 RAII
AVStream* Demuxer::getAVStream(MediaType type) const {
  int idx = getStreamIndex(type);
  if (idx >= 0 && format_ctx_) {
    return format_ctx_->streams[idx];
  }
  return nullptr;
}

PacketPtr Demuxer::pullPacket() {
  if (!format_ctx_) return nullptr;

  PacketPtr pkt(av_packet_alloc());
  if (!pkt) return nullptr;

  // av_read_frame 其实是读取一个数据块，并不是读取一帧
  int ret = av_read_frame(format_ctx_.get(), pkt.get());
  if (ret < 0) {
    if (ret == AVERROR_EOF) {
      if (!eof_) {
        eof_ = true;
        LOG_INFO << "Reached End Of File";
      }
    } else {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      LOG_ERROR << "Failed to pull packet. Reason: " << errbuf;
    }
    return nullptr;
  }

  return pkt;
}

bool Demuxer::seek(int64_t timestamp) {
  if (!format_ctx_) return false;

  int stream_index =
      video_stream_index_ >= 0 ? video_stream_index_ : audio_stream_index_;
  if (stream_index < 0) {
    LOG_ERROR << "Failed to seek. Reason: No valid stream for demuxer to seek";
    return false;
  }

  AVStream* stream = format_ctx_->streams[stream_index];

  // 1. 将用户传入的微秒 (AV_TIME_BASE_Q) 转换为码流的刻度 (time_base)
  int64_t seek_target =
      av_rescale_q(timestamp, AV_TIME_BASE_Q, stream->time_base);

  LOG_DEBUG << "Demxuer first seeking to " << timestamp << "us ";

  // 2. 跳转至目标时间戳的后向最近关键包
  int ret = av_seek_frame(format_ctx_.get(), stream_index, seek_target,
                          AVSEEK_FLAG_BACKWARD);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Failed to seek to " << timestamp << "us. Reason: " << errbuf;
    return false;
  }

  // 3. 跳转之后清除 AVFormatContext 内部缓冲区，并重置 EOF 标志
  avformat_flush(format_ctx_.get());
  eof_ = false;
  return true;
}

int64_t Demuxer::getDuration() const {
  if (!format_ctx_) return 0;

  if (format_ctx_->duration != AV_NOPTS_VALUE) {
    return format_ctx_->duration;
  }

  return 0;
}

}  // namespace avplayer
