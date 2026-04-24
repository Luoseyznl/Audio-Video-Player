#include "demuxer.h"

#include "logger.h"

extern "C" {
#include <libavutil/avutil.h>  // av_rescale_q
}

using namespace utils;

namespace avplayer {

Demuxer::~Demuxer() {
  LOG_INFO << "Destroying Demuxer";
  close();
}

bool Demuxer::open(const std::string& filename) {
  LOG_INFO << "Opening media file: " << filename;

  AVFormatContext* raw_ctx = nullptr;
  if (avformat_open_input(&raw_ctx, filename.c_str(), nullptr, nullptr) < 0) {
    LOG_ERROR << "Could not open input file: " << filename;
    return false;
  }
  format_ctx_.reset(raw_ctx);

  if (avformat_find_stream_info(format_ctx_.get(), nullptr) < 0) {
    LOG_ERROR << "Could not find stream information";
    close();
    return false;
  }

  video_stream_index_ = av_find_best_stream(
      format_ctx_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video_stream_index_ >= 0) {
    LOG_INFO << "Found video stream at index " << video_stream_index_;
  }

  audio_stream_index_ = av_find_best_stream(
      format_ctx_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audio_stream_index_ >= 0) {
    LOG_INFO << "Found audio stream at index " << audio_stream_index_;
  }

  return true;
}

void Demuxer::close() {
  LOG_INFO << "Closing demuxer";
  format_ctx_.reset();

  video_stream_index_ = -1;
  audio_stream_index_ = -1;
  eof_ = false;
}

int Demuxer::getStreamIndex(MediaType type) const {
  if (type == MediaType::Video) return video_stream_index_;
  if (type == MediaType::Audio) return audio_stream_index_;
  return -1;
}

AVStream* Demuxer::getAVStream(MediaType type) const {
  int idx = getStreamIndex(type);
  if (idx >= 0 && format_ctx_) {
    return format_ctx_->streams[idx];
  }
  return nullptr;
}

PacketPtr Demuxer::pullPacket() {
  if (!format_ctx_) {
    LOG_ERROR << "Demuxer not initialized";
    return nullptr;
  }

  PacketPtr pkt(av_packet_alloc());
  if (!pkt) return nullptr;

  int ret = av_read_frame(format_ctx_.get(), pkt.get());
  if (ret < 0) {
    if (ret == AVERROR_EOF) {
      eof_ = true;
      LOG_INFO << "Reached end of file";
    } else {
      char errbuf[AV_ERROR_MAX_STRING_SIZE];
      av_strerror(ret, errbuf, sizeof(errbuf));
      LOG_ERROR << "Error reading packet: " << errbuf;
    }
    return nullptr;
  }

  return pkt;
}

bool Demuxer::seek(int64_t timestamp, int flags) {
  if (!format_ctx_) return false;

  // 寻找参照流（优先使用视频流的时间基准）
  int stream_index =
      video_stream_index_ >= 0 ? video_stream_index_ : audio_stream_index_;
  if (stream_index < 0) {
    LOG_ERROR << "No valid stream for seeking";
    return false;
  }

  AVStream* stream = format_ctx_->streams[stream_index];

  // 计算目标时间戳所在的刻度（ticks）
  int64_t seek_target =
      av_rescale_q(timestamp, AV_TIME_BASE_Q, stream->time_base);

  LOG_DEBUG << "Seeking to " << timestamp << "us (target: " << seek_target
            << ")";

  int ret = av_seek_frame(format_ctx_.get(), stream_index, seek_target, flags);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Error seeking to position " << timestamp << "us: " << errbuf;
    return false;
  }

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
