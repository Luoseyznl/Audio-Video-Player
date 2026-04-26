#pragma once

#include <SDL2/SDL.h>

#include <atomic>
#include <vector>

#include "decoder.h"
#include "mediadefs.h"
#include "safe_queue.h"

namespace avplayer {

class AudioRenderer {
 public:
  AudioRenderer() = default;
  ~AudioRenderer();

  AudioRenderer(const AudioRenderer&) = delete;
  AudioRenderer& operator=(const AudioRenderer&) = delete;

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
  SwrContextPtr swr_ctx_;

  int out_channels_ = 2;
  int out_sample_rate_ = 44100;
  AVSampleFormat out_sample_fmt_ = AV_SAMPLE_FMT_S16;

  std::atomic<int64_t> audio_clock_{0};

  utils::SafeQueue<FramePtr> frame_queue_;

  std::vector<uint8_t> audio_buf_;
  int audio_buf_index_ = 0;
  int audio_buf_size_ = 0;

  int volume_ = SDL_MIX_MAXVOLUME;
};

}  // namespace avplayer
