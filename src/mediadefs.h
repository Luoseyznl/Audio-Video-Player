#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <memory>

namespace avplayer {

/**
 * FFmpeg 四大核心数据结构说明：
 * * 1. AVFormatContext 格式上下文：包含全局信息（总时长、码率、流数等）
 * - 通过 avformat_open_input() 打开，通过 avformat_close_input() 关闭并释放。
 * * 2. AVStream 流/轨道：具体的独立通道（视频流、音频流、字幕流等）
 * - 归属于 AVFormatContext（同时打开和释放），【不能】手动释放。
 * * 3. AVPacket 数据包：解封装后、解码前的压缩数据（H.264 视频包、AAC 音频包）
 * - 通过 av_packet_alloc() 创建或 av_read_frame() 填充，av_packet_free() 释放。
 * * 4. AVFrame 原始帧：解码后的原始数据（如 YUV 像素矩阵、PCM 模拟信号）
 * - 通过 av_frame_alloc() 创建或由解码器输出，av_frame_free() 释放。
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

}  // namespace avplayer