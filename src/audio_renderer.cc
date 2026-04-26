#include "audio_renderer.h"

#include "logger.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

using namespace utils;

namespace avplayer {

AudioRenderer::~AudioRenderer() {
  LOG_INFO << "AudioRenderer Destroying";
  close();
}

bool AudioRenderer::open(const Decoder::StreamInfo& info) {
  LOG_INFO << "Initializing AudioRenderer with " << info.sample_rate << "Hz, "
           << info.channels << " channels";

  if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
      LOG_ERROR << "Failed to initialize SDL audio. Reason: " << SDL_GetError();
      return false;
    }
  }

  out_sample_rate_ = info.sample_rate;
  out_channels_ = 2;
  out_sample_fmt_ = AV_SAMPLE_FMT_S16;  // 16 位采样格式

  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = out_sample_rate_;
  want.format = AUDIO_S16SYS;
  want.channels = out_channels_;
  want.samples = 1024;
  want.callback = audioCallback;
  want.userdata = this;  // 把当前对象的指针传给回调，方便在静态函数里调用成员

  device_id_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (device_id_ == 0) {
    LOG_ERROR << "Failed to open audio device: " << SDL_GetError();
    return false;
  }

  AVChannelLayout in_ch_layout, out_ch_layout;
  av_channel_layout_default(&in_ch_layout, info.channels);
  av_channel_layout_default(&out_ch_layout, out_channels_);

  SwrContext* raw_swr = nullptr;
  int ret = swr_alloc_set_opts2(
      &raw_swr, &out_ch_layout, out_sample_fmt_, out_sample_rate_,
      &in_ch_layout, info.sample_format, info.sample_rate, 0, nullptr);

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

  audio_clock_ = 0;
  LOG_INFO << "AudioRenderer successfully opened";
  return true;
}

void AudioRenderer::close() {
  stop();

  if (device_id_) {
    SDL_CloseAudioDevice(device_id_);
    device_id_ = 0;
  }

  if (SDL_WasInit(SDL_INIT_AUDIO)) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }

  swr_ctx_.reset();

  frame_queue_.clear();
  audio_buf_.clear();
  audio_buf_index_ = 0;
  audio_buf_size_ = 0;
}

void AudioRenderer::play() {
  if (device_id_) {
    SDL_PauseAudioDevice(device_id_, 0);
    LOG_INFO << "Audio playback started";
  }
}

void AudioRenderer::pause() {
  if (device_id_) {
    SDL_PauseAudioDevice(device_id_, 1);
    LOG_INFO << "Audio playback paused";
  }
}

void AudioRenderer::stop() {
  pause();
  if (device_id_) {
    SDL_LockAudioDevice(device_id_);  // 上锁
    clearBufferedAudio();
    SDL_UnlockAudioDevice(device_id_);  // 解锁
  } else {
    clearBufferedAudio();
  }
}

void AudioRenderer::enqueueFrame(FramePtr frame) {
  if (frame) {
    frame_queue_.push(std::move(frame));
  }
}

void AudioRenderer::audioCallback(void* userdata, uint8_t* stream, int len) {
  AudioRenderer* renderer = static_cast<AudioRenderer*>(userdata);
  renderer->fillAudioData(stream, len);
}

void AudioRenderer::fillAudioData(uint8_t* stream, int len) {
  SDL_memset(stream, 0, len);  // 防爆音

  int len_remaining = len;
  int stream_pos = 0;

  while (len_remaining > 0) {
    // 1. 缓冲区播放完毕，重新取帧
    if (audio_buf_index_ >= audio_buf_size_) {
      audio_buf_index_ = 0;
      audio_buf_size_ = 0;

      FramePtr frame;
      if (!frame_queue_.try_pop(frame) || !frame) {
        break;  // 非阻塞取帧，防止回调函数被阻塞
      }

      int out_samples = av_rescale_rnd(
          swr_get_delay(swr_ctx_.get(), frame->sample_rate) + frame->nb_samples,
          out_sample_rate_, frame->sample_rate, AV_ROUND_UP);

      int bytes_per_sample = av_get_bytes_per_sample(out_sample_fmt_);
      int out_size = out_samples * out_channels_ * bytes_per_sample;

      if (audio_buf_.size() < out_size) {
        audio_buf_.resize(out_size);  // 扩充
      }

      uint8_t* out_data[1] = {audio_buf_.data()};

      int converted_samples = swr_convert(
          swr_ctx_.get(), out_data, out_samples,
          const_cast<const uint8_t**>(frame->data), frame->nb_samples);

      if (converted_samples < 0) {
        LOG_ERROR << "Failed to resample audio";
        break;
      }

      audio_buf_size_ = converted_samples * out_channels_ * bytes_per_sample;

      if (frame->pts != AV_NOPTS_VALUE) {
        audio_clock_ = frame->pts;  // 时钟校准
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
          (out_sample_rate_ * out_channels_ * 2);
      audio_clock_ += time_increment_us;
    }
  }
}

void AudioRenderer::clearBufferedAudio() {
  frame_queue_.clear();
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
