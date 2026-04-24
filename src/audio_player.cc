#include "audio_player.h"

#include "logger.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

using namespace utils;

namespace avplayer {

AudioPlayer::~AudioPlayer() {
  LOG_INFO << "Destroying AudioPlayer";
  close();
}

bool AudioPlayer::open(const Decoder::StreamInfo& info) {
  LOG_INFO << "Opening AudioPlayer. Input: " << info.sample_rate << "Hz, "
           << info.channels << " channels";

  if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
      LOG_ERROR << "SDL audio initialization failed: " << SDL_GetError();
      return false;
    }
  }

  out_sample_rate_ = info.sample_rate;  // 保持和原音频相同的采样率
  out_channels_ = 2;                    // 双声道立体声
  out_sample_fmt_ = AV_SAMPLE_FMT_S16;  // 16 位整型 (声卡最喜欢的格式)

  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = out_sample_rate_;
  want.format = AUDIO_S16SYS;
  want.channels = out_channels_;
  want.samples = 1024;  // 缓冲区大小，决定了回调的频繁程度 (降低延迟)
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

  int ret = swr_alloc_set_opts2(
      &swr_ctx_, &out_ch_layout, out_sample_fmt_, out_sample_rate_,
      &in_ch_layout, info.sample_format, info.sample_rate, 0, nullptr);

  av_channel_layout_uninit(&in_ch_layout);
  av_channel_layout_uninit(&out_ch_layout);

  if (ret < 0 || !swr_ctx_) {
    LOG_ERROR << "Failed to allocate resampler context";
    return false;
  }

  if (swr_init(swr_ctx_) < 0) {
    LOG_ERROR << "Failed to initialize resampler context";
    return false;
  }

  audio_clock_ = 0;
  LOG_INFO << "AudioPlayer successfully opened";
  return true;
}

void AudioPlayer::close() {
  stop();

  if (device_id_) {
    SDL_CloseAudioDevice(device_id_);
    device_id_ = 0;
  }

  if (swr_ctx_) {
    swr_free(&swr_ctx_);
    swr_ctx_ = nullptr;
  }

  if (SDL_WasInit(SDL_INIT_AUDIO)) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }

  frame_queue_.clear();
  audio_buf_.clear();
  audio_buf_index_ = 0;
}

void AudioPlayer::play() {
  if (device_id_) {
    SDL_PauseAudioDevice(device_id_, 0);  // 0 代表取消暂停，开始播放
    LOG_INFO << "Audio playback started";
  }
}

void AudioPlayer::pause() {
  if (device_id_) {
    SDL_PauseAudioDevice(device_id_, 1);  // 1 代表暂停
    LOG_INFO << "Audio playback paused";
  }
}

void AudioPlayer::stop() {
  pause();
  if (device_id_) {
    SDL_LockAudioDevice(device_id_);
    clearBufferedAudio();
    SDL_UnlockAudioDevice(device_id_);
  } else {
    clearBufferedAudio();
  }
}

void AudioPlayer::enqueueFrame(FramePtr frame) {
  if (frame) {
    frame_queue_.push(std::move(frame));
  }
}

void AudioPlayer::audioCallback(void* userdata, uint8_t* stream, int len) {
  AudioPlayer* player = static_cast<AudioPlayer*>(userdata);
  player->fillAudioData(stream, len);
}

void AudioPlayer::fillAudioData(uint8_t* stream, int len) {
  // 核心防御：一开始先把要传给声卡的缓冲区涂成 0 (静音)
  // 如果后面没数据，声卡播的就是静音，而不会是上一次残留的刺耳爆音
  SDL_memset(stream, 0, len);

  int len_remaining = len;
  int stream_pos = 0;

  while (len_remaining > 0) {
    // 如果当前的缓存已经被声卡吃光了，就去队列里拿新的一帧
    if (audio_buf_index_ >= audio_buf_.size()) {
      audio_buf_.clear();
      audio_buf_index_ = 0;

      FramePtr frame;
      // 注意：这里必须用 try_pop
      // (非阻塞)！如果没拿到，说明解码慢了，直接退出循环播放静音
      if (!frame_queue_.try_pop(frame) || !frame) {
        break;  // 没数据了，直接跳出，把剩下的时间用静音填补
      }

      // 计算重采样后会有多少个样本
      int out_samples = av_rescale_rnd(
          swr_get_delay(swr_ctx_, frame->sample_rate) + frame->nb_samples,
          out_sample_rate_, frame->sample_rate, AV_ROUND_UP);

      int bytes_per_sample = av_get_bytes_per_sample(out_sample_fmt_);
      int out_size = out_samples * out_channels_ * bytes_per_sample;

      audio_buf_.resize(out_size);
      uint8_t* out_data[1] = {audio_buf_.data()};

      // 执行重采样：将 frame 里的数据转换并填入 audio_buf_
      int converted_samples = swr_convert(
          swr_ctx_, out_data, out_samples,
          const_cast<const uint8_t**>(frame->data), frame->nb_samples);

      if (converted_samples < 0) {
        LOG_ERROR << "Audio resampling failed";
        audio_buf_.clear();
        break;
      }

      // 缩减到实际转换的真实大小
      audio_buf_.resize(converted_samples * out_channels_ * bytes_per_sample);
    }

    // 计算这次能喂给声卡多少字节
    int bytes_avail = audio_buf_.size() - audio_buf_index_;
    int bytes_to_copy = std::min(len_remaining, bytes_avail);

    if (bytes_to_copy > 0) {
      // 用 SDL 的混音函数拷贝数据，SDL_MIX_MAXVOLUME 表示 100% 音量
      SDL_MixAudioFormat(stream + stream_pos,
                         audio_buf_.data() + audio_buf_index_, AUDIO_S16SYS,
                         bytes_to_copy, volume_);

      audio_buf_index_ += bytes_to_copy;
      stream_pos += bytes_to_copy;
      len_remaining -= bytes_to_copy;

      // ==========================================
      // 时钟累加器：计算刚才消耗的字节等于多少微秒 (us)
      // 计算公式：播放时长(秒) = 字节数 / (采样率 * 声道数 * 每个样本字节数)
      // ==========================================
      double added_time_sec = static_cast<double>(bytes_to_copy) /
                              (out_sample_rate_ * out_channels_ * 2);
      audio_clock_ += static_cast<int64_t>(added_time_sec * 1000000.0);
    }
  }
}

void AudioPlayer::clearBufferedAudio() {
  frame_queue_.clear();
  audio_buf_.clear();
  audio_buf_index_ = 0;
}

void AudioPlayer::setVolume(double norm) {
  if (norm < 0.0) norm = 0.0;
  if (norm > 1.0) norm = 1.0;
  // SDL 的音量范围是 0 ~ 128
  volume_ = static_cast<int>(norm * SDL_MIX_MAXVOLUME);
  LOG_INFO << "Audio volume set to: " << norm * 100 << "%";
}

double AudioPlayer::getVolume() const {
  return static_cast<double>(volume_) / SDL_MIX_MAXVOLUME;
}

}  // namespace avplayer
