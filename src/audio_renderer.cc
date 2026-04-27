#include "audio_renderer.h"

#include "logger.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

using namespace utils;

namespace avplayer {

AudioRenderer::~AudioRenderer() {
  LOG_INFO << "Destroying AudioRenderer";
  close();
}

bool AudioRenderer::open(const Decoder::StreamInfo& info,
                         AVRational time_base) {
  LOG_INFO << "Initializing AudioRenderer with " << info.sample_rate << "Hz, "
           << info.channels << " channels";

  // 1. 初始化 SDL 音频子系统（声卡回调机制）
  if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
      LOG_ERROR << "Failed to initialize SDL audio. Reason: " << SDL_GetError();
      return false;
    }
  }

  // 2. 统一音频格式：采样率不变、双声道、S16
  out_sample_rate_ = info.sample_rate;
  out_channels_ = 2;
  out_sample_fmt_ = AV_SAMPLE_FMT_S16;  // 16 位采样格式

  // 3. 配置 SDL 音频拉取回调（Pull Model）
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = out_sample_rate_;
  want.format = AUDIO_S16SYS;
  want.channels = out_channels_;
  want.samples = 1024;
  want.callback = audioCallback;
  want.userdata = this;  // 把当前对象的指针传给回调，方便在静态函数里调用成员
  device_id_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);  // 打开设备
  if (device_id_ == 0) {
    LOG_ERROR << "Failed to open audio device: " << SDL_GetError();
    return false;
  }

  // 4. 配置 FFmpeg 重采样上下文并启动转换引擎
  AVChannelLayout in_ch_layout, out_ch_layout;
  av_channel_layout_default(&in_ch_layout, info.channels);   // 输入帧的声道布局
  av_channel_layout_default(&out_ch_layout, out_channels_);  // 输出帧的声道布局
  SwrContext* raw_swr = nullptr;
  int ret = swr_alloc_set_opts2(
      &raw_swr,                                             //
      &out_ch_layout, out_sample_fmt_, out_sample_rate_,    // SDL want 采样格式
      &in_ch_layout, info.sample_format, info.sample_rate,  // 解码器输入的格式
      0, nullptr);
  av_channel_layout_uninit(&in_ch_layout);
  av_channel_layout_uninit(&out_ch_layout);
  if (ret < 0 || !raw_swr) {
    LOG_ERROR << "Failed to allocate resampler context";
    return false;
  }
  swr_ctx_.reset(raw_swr);
  if (swr_init(swr_ctx_.get()) < 0) {
    LOG_ERROR << "Failed to initialize resampler context";
    swr_ctx_.reset();
    return false;
  }

  // 5. 初始化主时钟
  audio_clock_us_ = 0;
  time_base_ = time_base;

  LOG_INFO << "AudioRenderer successfully opened";
  return true;
}

void AudioRenderer::close() {
  stop();

  if (device_id_) {
    SDL_CloseAudioDevice(device_id_);  // 关闭设备
    device_id_ = 0;
  }

  if (SDL_WasInit(SDL_INIT_AUDIO)) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);  // 退出 SDL 子系统
  }

  swr_ctx_.reset();

  frame_queue_.clear();
  audio_buf_.clear();
  audio_buf_index_ = 0;
  audio_buf_size_ = 0;
}

void AudioRenderer::play() {
  if (device_id_) {
    SDL_PauseAudioDevice(device_id_, 0);  // 解开静音锁
    LOG_INFO << "Audio playback started";
  }
}

void AudioRenderer::pause() {
  if (device_id_) {
    SDL_PauseAudioDevice(device_id_, 1);  // 锁上静音锁
    LOG_INFO << "Audio playback paused";
  }
}

void AudioRenderer::stop() {
  pause();
  if (device_id_) {
    SDL_LockAudioDevice(device_id_);  // SDL 提供的声卡级互斥锁
    clearBufferedAudio();
    SDL_UnlockAudioDevice(device_id_);
  } else {
    clearBufferedAudio();
  }
}

// 解码器推入 AVFrame
void AudioRenderer::enqueueFrame(FramePtr frame) {
  if (frame) {
    frame_queue_.push(std::move(frame));
  }
}

// 声卡驱动通过中断回调该函数，获取 len 长度的字节流
void AudioRenderer::audioCallback(void* userdata, uint8_t* stream, int len) {
  AudioRenderer* renderer = static_cast<AudioRenderer*>(userdata);  // this 指针
  renderer->fillAudioData(stream, len);
}

// 线性填音并更新主时钟（因为每一帧重采样后长度不一，没必要环形填音）
void AudioRenderer::fillAudioData(uint8_t* stream, int len) {
  SDL_memset(stream, 0, len);  // 防爆音

  int len_remaining = len;
  int stream_pos = 0;

  while (len_remaining > 0) {
    // 1. 本地重采样后的缓存（audio_buf_）已经播完，需要从队列里拿新的一帧
    if (audio_buf_index_ >= audio_buf_size_) {
      audio_buf_index_ = 0;
      audio_buf_size_ = 0;

      FramePtr frame;
      if (!frame_queue_.try_pop(frame) || !frame) {
        break;  // 非阻塞取帧，防止回调函数被阻塞
      }

      // 计算重采样后可能产生的目标样本数，预留足够的输出空间
      int out_samples = av_rescale_rnd(
          swr_get_delay(swr_ctx_.get(), frame->sample_rate) + frame->nb_samples,
          out_sample_rate_, frame->sample_rate, AV_ROUND_UP);

      int bytes_per_sample = av_get_bytes_per_sample(out_sample_fmt_);
      int out_size = out_samples * out_channels_ * bytes_per_sample;

      if (audio_buf_.size() < out_size) {
        audio_buf_.resize(out_size);  // 提前扩容，只扩不缩
      }

      // 重采样：转换采样率、位深、声道布局，并交织存放（Interleaved）
      uint8_t* out_data[1] = {audio_buf_.data()};  // 指针数组
      int converted_samples = swr_convert(
          swr_ctx_.get(), out_data, out_samples,
          const_cast<const uint8_t**>(frame->data), frame->nb_samples);
      if (converted_samples < 0) {
        LOG_ERROR << "Failed to resample audio";
        break;
      }
      // 重采样后的本地缓存内存大小（字节数）
      audio_buf_size_ = converted_samples * out_channels_ * bytes_per_sample;

      if (frame->pts != AV_NOPTS_VALUE) {
        audio_clock_us_ = av_rescale_q(frame->pts, time_base_, AV_TIME_BASE_Q);
      }
    }

    int bytes_avail = audio_buf_size_ - audio_buf_index_;
    int bytes_to_copy = std::min(len_remaining, bytes_avail);

    if (bytes_to_copy > 0) {
      SDL_MixAudioFormat(stream + stream_pos,
                         audio_buf_.data() + audio_buf_index_, AUDIO_S16SYS,
                         bytes_to_copy, volume_);

      audio_buf_index_ += bytes_to_copy;
      stream_pos += bytes_to_copy;
      len_remaining -= bytes_to_copy;

      int64_t time_increment_us =
          (static_cast<int64_t>(bytes_to_copy) * AV_TIME_BASE) /
          (out_sample_rate_ * out_channels_ * 2);  // 时钟增长
      audio_clock_us_ += time_increment_us;
    }
  }
}

void AudioRenderer::clearBufferedAudio() {
  frame_queue_.clear();  // 清空残留帧
  audio_buf_index_ = 0;
  audio_buf_size_ = 0;
}

void AudioRenderer::setVolume(double norm) {
  if (norm < 0.0) norm = 0.0;
  if (norm > 1.0) norm = 1.0;
  volume_ = static_cast<int>(norm * SDL_MIX_MAXVOLUME);
  LOG_INFO << "Audio volume set to: " << norm * 100 << "%";
}

double AudioRenderer::getVolume() const {
  return static_cast<double>(volume_) / SDL_MIX_MAXVOLUME;
}

}  // namespace avplayer
