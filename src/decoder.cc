#include "decoder.h"

#include "logger.h"

extern "C" {
#include <libavutil/pixdesc.h>    // 供 av_get_pix_fmt_name 使用
#include <libavutil/samplefmt.h>  // 供 av_get_sample_fmt_name 使用
}

using namespace utils;

namespace avplayer {

Decoder::~Decoder() {
  LOG_INFO << "Destroying Decoder";
  close();
}

bool Decoder::open(const AVStream* stream) {
  if (!stream || !stream->codecpar) {
    LOG_ERROR << "Invalid stream or codec parameters";
    return false;
  }

  // 1. 查找对应的解码器
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    LOG_ERROR << "Codec not found for ID: " << stream->codecpar->codec_id;
    return false;
  }

  // 2. 分配解码器上下文
  AVCodecContext* raw_ctx = avcodec_alloc_context3(codec);
  if (!raw_ctx) {
    LOG_ERROR << "Could not allocate codec context";
    return false;
  }
  codec_ctx_.reset(raw_ctx);

  // 3. 将流中的参数拷贝到解码器上下文中
  if (avcodec_parameters_to_context(codec_ctx_.get(), stream->codecpar) < 0) {
    LOG_ERROR << "Could not copy codec params to context";
    close();
    return false;
  }

  // 4. 打开解码器
  if (avcodec_open2(codec_ctx_.get(), codec, nullptr) < 0) {
    LOG_ERROR << "Could not open codec";
    close();
    return false;
  }

  // 5. 登记 StreamInfo (方便日志打印和下游渲染器初始化)
  if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    info_.type = MediaType::Video;
    info_.width = codec_ctx_->width;
    info_.height = codec_ctx_->height;
    info_.pixel_format = codec_ctx_->pix_fmt;

    LOG_INFO << "Video decoder initialized: " << info_.width << "x"
             << info_.height
             << ", Pixel format: " << av_get_pix_fmt_name(info_.pixel_format);

  } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
    info_.type = MediaType::Audio;
    info_.sample_rate = codec_ctx_->sample_rate;
    info_.channels = codec_ctx_->ch_layout.nb_channels;
    info_.sample_format = codec_ctx_->sample_fmt;

    LOG_INFO << "Audio decoder initialized: " << info_.sample_rate << "Hz, "
             << info_.channels << " channels, Format: "
             << av_get_sample_fmt_name(info_.sample_format);
  } else {
    LOG_WARN << "Unknown stream type decoded";
  }

  return true;
}

void Decoder::close() {
  if (codec_ctx_) {
    LOG_INFO << "Closing decoder";
    codec_ctx_.reset();  // 自动调用 avcodec_free_context
  }
}

void Decoder::flush() {
  if (codec_ctx_) {
    LOG_INFO << "Flushing decoder internal buffers";
    avcodec_flush_buffers(codec_ctx_.get());
  }
}

bool Decoder::pushPacket(const PacketPtr& pkt) {
  if (!codec_ctx_) {
    LOG_ERROR << "Decoder not initialized";
    return false;
  }

  // 若 pkt 为 nullptr，get() 会返回 nullptr，在 FFmpeg 中代表发送 EOF 冲刷指令
  int ret = avcodec_send_packet(codec_ctx_.get(), pkt ? pkt.get() : nullptr);

  if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Error pushing packet for decoding: " << errbuf;
    return false;
  }
  return true;
}

FramePtr Decoder::pullFrame() {
  if (!codec_ctx_) return nullptr;

  FramePtr frame(av_frame_alloc());
  if (!frame) return nullptr;

  int ret = avcodec_receive_frame(codec_ctx_.get(), frame.get());

  if (ret == AVERROR(EAGAIN)) {
    return nullptr;
  } else if (ret == AVERROR_EOF) {
    LOG_DEBUG << "Decoder reached EOF";
    return nullptr;
  } else if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    LOG_ERROR << "Error pulling frame: " << errbuf;
    return nullptr;
  }

  return frame;
}

}  // namespace avplayer