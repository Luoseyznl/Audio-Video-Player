#include "decoder.h"

#include "logger.h"

extern "C" {
#include <libavutil/pixdesc.h>    // av_get_pix_fmt_name
#include <libavutil/samplefmt.h>  // av_get_sample_fmt_name
}

using namespace utils;

namespace avplayer {

// 实例化解码器上下文 AVCodecContext 并传入解码参数
bool Decoder::open(const AVStream* stream) {
  if (!stream || !stream->codecpar) {
    LOG_ERROR << "Failed to open decoder. Reason: Invalid stream or codecpar";
    return false;
  }

  // 1. 查找对应的解码器
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    LOG_ERROR << "Failed to find decoder. Reason: Unsupported codec ID "
              << stream->codecpar->codec_id;
    return false;
  }

  // 2. 分配解码器上下文
  AVCodecContext* raw_ctx = avcodec_alloc_context3(codec);
  if (!raw_ctx) {
    LOG_ERROR << "Failed to allocate codec context. Reason: Out of memory";
    return false;
  }
  raw_ctx->thread_count = 0;  // 0 表示 FFmepg 自动根据 CPU 核心数调度多线程解码
  codec_ctx_.reset(raw_ctx);

  // 3. 将解码参数拷贝到解码器上下文中
  int ret = avcodec_parameters_to_context(codec_ctx_.get(), stream->codecpar);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Failed to copy codec params to context. Reason: " << errbuf;
    close();
    return false;
  }

  // 4. 开启解码器
  ret = avcodec_open2(codec_ctx_.get(), codec, nullptr);
  if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Failed to open codec. Reason: " << errbuf;
    close();
    return false;
  }

  if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    info_.type = MediaType::Video;
    info_.width = codec_ctx_->width;
    info_.height = codec_ctx_->height;
    info_.pixel_format = codec_ctx_->pix_fmt;

    const char* fmt_name = av_get_pix_fmt_name(info_.pixel_format);
    LOG_INFO << "Video Decoder info: " << info_.width << "x" << info_.height
             << ", Pixel format: " << (fmt_name ? fmt_name : "Unknown");

  } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
    info_.type = MediaType::Audio;
    info_.sample_rate = codec_ctx_->sample_rate;
    info_.channels = codec_ctx_->ch_layout.nb_channels;
    info_.sample_format = codec_ctx_->sample_fmt;

    const char* fmt_name = av_get_sample_fmt_name(info_.sample_format);
    LOG_INFO << "Audio Decoder info: " << info_.sample_rate << "Hz, "
             << info_.channels
             << " channels, Format: " << (fmt_name ? fmt_name : "Unknown");
  } else {
    LOG_WARN << "Unknown codec type";
  }

  return true;
}

void Decoder::close() {
  if (codec_ctx_) {
    codec_ctx_.reset();  // 自动调用 avcodec_free_context
  }
}

void Decoder::flush() {
  if (codec_ctx_) {
    LOG_INFO << "Decoder flushing buffers";
    avcodec_flush_buffers(codec_ctx_.get());  // 丢弃解码器内的残留帧
  }
}

// 传入数据包，若传入 nullptr 代表排空残留帧（Drain Mode）
bool Decoder::pushPacket(const PacketPtr& pkt) {
  if (!codec_ctx_) return false;

  int ret = avcodec_send_packet(codec_ctx_.get(), pkt ? pkt.get() : nullptr);

  if (ret == 0) {
    return true;
  } else if (ret == AVERROR(EAGAIN)) {
    // EAGAIN 表示解码器缓冲区已满
    return false;
  } else if (ret == AVERROR_EOF) {
    // EOF 表示解码器处于 Drain Mode
    return false;
  }

  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(ret, errbuf, sizeof(errbuf));
  LOG_ERROR << "Failed to push packet for decoding. Reason: " << errbuf;
  return false;
}

// 从解码器取出原始帧，若返回 nullptr 代表解码器饥饿或已排空
FramePtr Decoder::pullFrame() {
  if (!codec_ctx_) return nullptr;

  FramePtr frame(av_frame_alloc());
  if (!frame) return nullptr;

  int ret = avcodec_receive_frame(codec_ctx_.get(), frame.get());
  if (ret == 0) {
    return frame;
  } else if (ret == AVERROR(EAGAIN)) {
    return nullptr;
  } else if (ret == AVERROR_EOF) {
    LOG_INFO << "Decoder reached EOF (Drained)";
    return nullptr;
  }

  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(ret, errbuf, sizeof(errbuf));
  LOG_ERROR << "Failed to pull frame. Reason: " << errbuf;
  return nullptr;
}

}  // namespace avplayer