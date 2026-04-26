#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <memory>

namespace avplayer {

/**
 * AVPlayer Data Flow Architecture:
 * 1. Demuxing:   LA.mp4 -> Demuxer (AVFormatContext/AVStream) -> AVPacket
 * 2. Decoding:   AVPacket -> Decoder (AVCodecContext) -> AVFrame
 * 3. Processing:
 * ├── Audio:  AVFrame -> Resampler (SwrContext) -> SDL/Audio Device
 * └── Video:  AVFrame -> Renderer (SwsContext / OpenGL) -> Screen
 */
enum class MediaType { Unknown = -1, Video = 0, Audio = 1 };

/**
 * AVFormatContext
 * ├── .iformat: 封装格式 mp4, mkv
 * ├── .duration: 总时长 (1 / AV_TIME_BASE)
 * ├── .nb_streams: 流数
 * └── .*streams[i]: AVStream 码流
 *      ├── .time_base: 时间基准
 *      └── .*codecpar: AVCodecParameters 解码参数
 *           ├── .codec_id: 编码格式（H264, AAC...）
 *           ├── .width / .height: 视频声明宽高
 *           ├── .sample_rate: 音频声明采样率
 *           └── .ch_layout / .nb_channels: 音频声明通道布局和通道数
 */
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

/**
 * AVPacket
 * ├── .*data / .size: 压缩后的码流
 * ├── .stream_index: 码流索引
 * ├── .pos: 包的字节偏移值（用于定位排错）
 * ├── .pts / .dts: 显示 / 解码时间戳（基于 time_base）
 * ├── .duration: 包的总时长 (1 / time_base)
 * └── .flags: 是否是关键帧 (AV_PKT_FLAG_KEY)
 */
struct AVPacketDeleter {
  void operator()(AVPacket* pkt) const {
    if (pkt) {
      av_packet_free(&pkt);
    }
  }
};
using PacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

/**
 * AVFrame
 * ├── .format: 像素格式(AVPixelFormat) 或 采样格式(AVSampleFormat)
 * ├── .*data[8]: 指向像素数据矩阵的指针数组
 * |    ├── 视频（YUV）：[0]=Y 亮度，[1]=U 色度，[2]=V 色度，[3]=alpha 透明度...
 * |    ├── 音频（Planner 模式）：[0]=左声道，[1]=右声道，[2]=次低音...
 * |    └── 音频（Packed 模式）：数据全部交错存在 [0] 中 (LRLRLR...)
 * ├── .linesize[8]: 通道长度（包含内存对齐）
 * |    ├── 视频：每一行像素的字节数 (Stride，包含内存对齐填充)
 * |    └── 音频：单声道内数据的字节数
 * ├── .width / .height: 视频实际宽高
 * ├── .sample_rate: 音频采样率
 * ├── .nb_samples: 音频采样数（音频帧时长 = 采样数 / 采样率）
 * ├── .ch_layout: 音频声道布局 (单声道、立体声、5.1等)
 * └── .pts: 显示时间戳
 */
struct AVFrameDeleter {
  void operator()(AVFrame* frame) const {
    if (frame) {
      av_frame_free(&frame);
    }
  }
};
using FramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

/**
 * AVCodecContext
 * ├── .*codec: AVCodec 解码器驱动
 * |    └── .name / .long_name: 驱动的名字
 * ├── .codec_id / .codec_type: 解码格式
 * ├── .*extradata / .extradata_size: 初始化头数据 SPS, PPS
 * ├── .width / .height: 视频实际宽高
 * |── .pix_fmt / .sample_fmt: 像素或采样格式
 * └── .thread_count: 并行解码线程数
 */
struct AVCodecContextDeleter {
  void operator()(AVCodecContext* ctx) const {
    if (ctx) {
      avcodec_free_context(&ctx);
    }
  }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

/**
 * SwrContext 音频重采样上下文
 */
struct SwrContextDeleter {
  void operator()(SwrContext* ctx) const {
    if (ctx) {
      swr_free(&ctx);
    }
  }
};
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

/**
 * SwsContext 视频图像转换上下文
 */
struct SwsContextDeleter {
  void operator()(SwsContext* ctx) const {
    if (ctx) {
      sws_freeContext(ctx);
    }
  }
};
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

}  // namespace avplayer