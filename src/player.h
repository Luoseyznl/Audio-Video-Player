#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <thread>

#include "audio_player.h"
#include "av_reader.h"
#include "decoder.h"
#include "gl_renderer.h"

// 提前声明 GLFWwindow，避免在这个头文件里引入过多的 OpenGL 依赖
struct GLFWwindow;

namespace avplayer {

class Player {
 public:
  enum class State { Stopped, Playing, Paused, Error };

  // 回调函数：用于将播放进度和状态抛给外部 UI (比如更新进度条)
  using TimestampCallback =
      std::function<void(double timestamp_s, double duration_s)>;
  using StateCallback = std::function<void(State state)>;

  Player();
  ~Player();

  // ==========================================
  // 对外暴露的极简 Facade 接口
  // ==========================================
  bool open(const std::string& filename);
  void close();

  void play();
  void pause();
  void resume();
  void stop();

  // 指定跳转到多少秒 (例如 1.5s)
  bool seek(double timestamp_s);

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

  // ==========================================
  // 核心后台引擎循环
  // ==========================================
  // 负责从 Reader 拉取视频包 -> 解码 -> 音视频同步计算 -> 交给 GLRenderer 渲染
  void videoDecodeAndRenderLoop();

  // 负责从 Reader 拉取音频包 -> 解码 -> 塞入 AudioPlayer 的缓存队列
  void audioDecodeLoop();

  // 内部辅助函数：获取主时钟 (以音频为准)
  int64_t getMasterClock() const;

  // 判断某个解码周期是否已经被新的 seek 废弃
  bool isSeekGenerationStale(uint64_t generation) const;
  int64_t getFramePtsUs(const AVFrame* frame, const AVStream* stream) const;
  int64_t getVideoFrameDurationUs(const AVFrame* frame,
                                  const AVStream* video_stream) const;
  bool interruptibleSleepFor(int64_t sleep_us, uint64_t generation);
  bool shouldDropFrameBeforeSeekTarget(int64_t pts_us, bool is_audio);

  // ==========================================
  // 子模块实例 (四大金刚)
  // ==========================================
  AVReader reader_;
  Decoder video_decoder_;
  Decoder audio_decoder_;
  GLRenderer renderer_;
  AudioPlayer audio_player_;

  // ==========================================
  // 线程与状态管理
  // ==========================================
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

  // 同步相关的基准变量
  std::atomic<int64_t> current_video_pts_{0};
  std::atomic<int64_t> seek_target_us_{AV_NOPTS_VALUE};
  std::atomic<bool> audio_seek_pending_{false};
  std::atomic<bool> video_seek_pending_{false};
  double total_duration_s_ = 0.0;
  int64_t last_video_pts_us_ = AV_NOPTS_VALUE;
  int64_t last_video_delay_us_ = 0;
};

}  // namespace avplayer
