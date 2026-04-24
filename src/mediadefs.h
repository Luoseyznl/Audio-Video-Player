#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <memory>

namespace avplayer {

/**
 * 1. Ingestion LA.mp4 -> Demuxer -> AVPacket -> Decoder -> AVFrame ->
 GLRenderer
 * ├── AVFormatContext
 * ├── AVStream
 * └── AVPacket
 * 2. Processing AVPacket -> Decoder
 *
 * FFmpeg 数据结构说明：
 * * 1. AVFormatContext 格式上下文：总时长、码率、流数等
 * - avformat_open_input() -> avformat_close_input()
 * * 2. AVStream 流/轨道：视频流、音频流、字幕流等，包含 time_base 和编码参数。
 * - 归属于 AVFormatContext（同时打开和释放），【不能】手动释放。
 * * 3. AVPacket 数据包：解封装后、解码前的压缩数据（H.264 视频包、AAC 音频包）
 * - av_packet_alloc() / av_read_frame() -> av_packet_free()
 * * 4. AVFrame 原始帧：YUV 像素矩阵、PCM 模拟信号
 * - av_frame_alloc() -> av_frame_free()
 * * 5. AVCodecContext 解码器上下文：具体流的解码工作
 * - avcodec_alloc_context3() / avcodec_parameters_to_context() /
 * avcodec_open2() -> avcodec_free_context()
 */
enum class MediaType { Unknown = -1, Video = 0, Audio = 1 };

// ==========================================
// AVPacket (压缩数据包)
// ==========================================
struct AVPacketDeleter {
  void operator()(AVPacket* pkt) const {
    if (pkt) {
      av_packet_free(&pkt);
    }
  }
};
using PacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

// ==========================================
// AVFrame (解码后的原始数据)
// ==========================================
struct AVFrameDeleter {
  void operator()(AVFrame* frame) const {
    if (frame) {
      av_frame_free(&frame);
    }
  }
};
using FramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

// ==========================================
// AVFormatContext (格式封装上下文)
// ==========================================
struct AVFormatContextDeleter {
  void operator()(AVFormatContext* ctx) const {
    if (ctx) {
      // avformat_close_input 需要传入二级指针，并且它内部会自动把 ctx 置空
      avformat_close_input(&ctx);
    }
  }
};
using FormatContextPtr =
    std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;

// ==========================================
// AVCodecContext (解码器上下文)
// ==========================================
struct AVCodecContextDeleter {
  void operator()(AVCodecContext* ctx) const {
    if (ctx) {
      avcodec_free_context(&ctx);
    }
  }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

}  // namespace avplayer