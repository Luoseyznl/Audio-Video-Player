#pragma once

#include <SDL2/SDL.h>

#include <atomic>
extern "C" {
#include <libswresample/swresample.h>
}

#include "decoder.h"
#include "mediadefs.h"
#include "safe_queue.h"

namespace avplayer {

/**
 * @brief SDL2 音频播放器 (也是播放器的主时钟源)
 * 职责：
 * 1. 初始化系统声卡，设置固定的输出格式 (如 44100Hz, S16, Stereo)。
 * 2. 接收解码器吐出的 AVFrame，利用 SwrContext 进行重采样。
 * 3. 响应声卡的回调 (Callback)，源源不断地输送 PCM 数据。
 * 4. 维护 audio_clock，供视频同步使用。
 */
class AudioPlayer {
 public:
  AudioPlayer() = default;
  ~AudioPlayer();

  bool open(const Decoder::StreamInfo& info);
  void close();

  void play();
  void pause();
  void stop();

  int64_t getAudioClock() const { return audio_clock_; }
  void resetClock(int64_t pts_us) { audio_clock_ = pts_us; }

  void setVolume(double norm);
  double getVolume() const;

  void enqueueFrame(FramePtr frame);

 private:
  static void audioCallback(void* userdata, uint8_t* stream, int len);

  void fillAudioData(uint8_t* stream, int len);
  void clearBufferedAudio();

  SDL_AudioDeviceID device_id_ = 0;
  SwrContext* swr_ctx_ = nullptr;

  int out_sample_rate_ = 44100;
  AVSampleFormat out_sample_fmt_ = AV_SAMPLE_FMT_S16;
  int out_channels_ = 2;

  std::atomic<int64_t> audio_clock_{0};

  utils::SafeQueue<FramePtr> frame_queue_;

  std::vector<uint8_t> audio_buf_;
  int audio_buf_index_ = 0;  // 当前缓存被声卡读到哪里了
  int volume_ = SDL_MIX_MAXVOLUME;
};

}  // namespace avplayer
