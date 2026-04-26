#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "audio_renderer.h"
#include "decoder.h"
#include "gl_renderer.h"
#include "packet_producer.h"

struct GLFWwindow;
struct AVFrame;
struct AVStream;

namespace avplayer {

class Player {
 public:
  enum class State { Stopped, Playing, Paused, Error };

  using TimestampCallback =
      std::function<void(double timestamp_s, double duration_s)>;
  using StateCallback = std::function<void(State state)>;

  Player();
  ~Player();

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  bool open(const std::string& filename);
  void close();
  void play();
  void pause();
  void resume();
  void stop();

  bool seek(double timestamp_s);  // 秒

  bool isFinished() const;
  double getDuration() const;          // 返回总时长(秒)
  double getCurrentTimestamp() const;  // 返回当前播放时间(秒)
  State getState() const { return state_; }
  GLFWwindow* getWindow() const;

  void setVolume(double norm);  // 0.0 ~ 1.0
  double getVolume() const;

  void setTimestampCallback(TimestampCallback callback) {
    timestamp_callback_ = std::move(callback);
  }
  void setStateCallback(StateCallback callback) {
    state_callback_ = std::move(callback);
  }

 private:
  void changeState(State new_state);

  void videoDecodeAndRenderLoop();
  void audioDecodeLoop();

  int64_t getMasterClock() const;

  bool isSeekGenerationStale(uint64_t generation) const;

  int64_t getFramePtsUs(const AVFrame* frame, const AVStream* stream) const;
  int64_t getVideoFrameDurationUs(const AVFrame* frame,
                                  const AVStream* video_stream) const;
  bool interruptibleSleepFor(int64_t sleep_us, uint64_t generation);
  bool shouldDropFrameBeforeSeekTarget(int64_t pts_us, bool is_audio);

  PacketProducer packet_producer_;
  Decoder video_decoder_;
  Decoder audio_decoder_;
  GLRenderer renderer_;
  AudioRenderer audio_renderer_;

  std::thread video_thread_;
  std::thread audio_thread_;

  std::atomic<bool> running_{false};
  std::atomic<State> state_{State::Stopped};

  std::atomic<uint64_t> seek_generation_{0};

  mutable std::mutex video_decoder_mutex_;
  mutable std::mutex audio_decoder_mutex_;
  mutable std::mutex seek_mutex_;

  TimestampCallback timestamp_callback_;
  StateCallback state_callback_;

  std::atomic<int64_t> current_video_pts_{0};
  std::atomic<int64_t> seek_target_us_{AV_NOPTS_VALUE};
  std::atomic<bool> audio_seek_pending_{false};
  std::atomic<bool> video_seek_pending_{false};
  double total_duration_s_ = 0.0;
  int64_t last_video_pts_us_ = AV_NOPTS_VALUE;
  int64_t last_video_delay_us_ = 0;
};

}  // namespace avplayer
