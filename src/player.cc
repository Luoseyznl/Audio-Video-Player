#include "player.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>

#include "logger.h"

extern "C" {
#include <libavutil/time.h>  // AV_TIME_BASE
}

using namespace utils;

namespace avplayer {

// 同步阈值设置 (微秒)
constexpr int64_t AV_SYNC_THRESHOLD_MIN = 40000;   // 轻微落后，可以接受
constexpr int64_t AV_SYNC_THRESHOLD_MAX = 100000;  // 严重脱节，强制丢帧
constexpr int64_t VIDEO_SLEEP_SLICE_US = 5000;     // 视频线程休眠切片时长

bool Player::open(const std::string& filename) {
  if (!packet_producer_.open(filename)) {
    LOG_ERROR << "Player failed to open media file";
    changeState(State::Error);
    return false;
  }

  total_duration_s_ =
      static_cast<double>(packet_producer_.getDuration()) / AV_TIME_BASE;
  last_video_pts_us_ = AV_NOPTS_VALUE;
  last_video_delay_us_ = 0;
  seek_target_us_ = AV_NOPTS_VALUE;
  audio_seek_pending_ = false;
  video_seek_pending_ = false;

  // 1. 初始化视频渲染器
  const AVStream* video_stream = packet_producer_.getVideoStream();
  if (video_stream) {
    if (video_decoder_.open(video_stream)) {
      auto info = video_decoder_.getStreamInfo();
      if (!renderer_.open(info.width, info.height, "AVPlayer")) {
        LOG_ERROR << "Failed to initialize GLRenderer";
        return false;
      }
    }
  }

  // 2. 初始化音频渲染器
  const AVStream* audio_stream = packet_producer_.getAudioStream();
  if (audio_stream) {
    if (audio_decoder_.open(audio_stream)) {
      if (!audio_renderer_.open(audio_decoder_.getStreamInfo(),
                                audio_stream->time_base)) {
        LOG_ERROR << "Failed to initialize AudioRenderer";
        return false;
      }
    }
  }

  if (!video_stream && !audio_stream) {
    LOG_ERROR << "No valid streams";
    return false;
  }

  // 启动取包引擎
  packet_producer_.start();

  // 启动音视频解码渲染双引擎
  running_ = true;
  if (video_stream) {
    video_thread_ = std::thread(&Player::videoDecodeAndRenderLoop, this);
  }
  if (audio_stream) {
    audio_thread_ = std::thread(&Player::audioDecodeLoop, this);
  }

  changeState(State::Stopped);  // 取包和解码流水线开始预热，但播放器还并未开始
  LOG_INFO << "Player preheating complete";
  return true;
}

void Player::close() {
  stop();
  running_ = false;

  packet_producer_.stop();

  if (video_thread_.joinable()) video_thread_.join();
  if (audio_thread_.joinable()) audio_thread_.join();

  packet_producer_.close();
  video_decoder_.close();
  audio_decoder_.close();
  renderer_.close();
  audio_renderer_.close();

  changeState(State::Stopped);
}

void Player::play() {
  if (state_ == State::Playing) return;
  if (state_ == State::Paused) {
    resume();
    return;
  }

  audio_renderer_.play();  // 启动声卡
  changeState(State::Playing);
}

void Player::pause() {
  if (state_ != State::Playing) return;
  audio_renderer_.pause();  // 暂停声卡
  changeState(State::Paused);
}

void Player::resume() {
  if (state_ != State::Paused) return;
  audio_renderer_.play();  // 恢复声卡
  changeState(State::Playing);
}

void Player::stop() {
  audio_renderer_.stop();  // 声卡停转，清空缓存
  changeState(State::Stopped);
  current_video_pts_ = 0;
  last_video_pts_us_ = AV_NOPTS_VALUE;
  last_video_delay_us_ = 0;
  seek_target_us_ = AV_NOPTS_VALUE;
  audio_seek_pending_ = false;
  video_seek_pending_ = false;
}

bool Player::seek(double timestamp_s) {
  std::lock_guard<std::mutex> seek_lock(seek_mutex_);
  LOG_INFO << "Player seeking to " << timestamp_s << " s";

  State old_state = state_;
  pause();

  // 1. 先计算并设置好所有的“目标数据”
  int64_t target_us = static_cast<int64_t>(timestamp_s * AV_TIME_BASE);
  target_us = std::clamp<int64_t>(
      target_us, 0, static_cast<int64_t>(total_duration_s_ * AV_TIME_BASE));
  seek_target_us_ = target_us;

  // 3. 更新世代号，并强制清空 Demuxer 的缓冲队列
  seek_generation_.fetch_add(1, std::memory_order_acq_rel);
  if (!packet_producer_.seek(target_us)) {
    LOG_ERROR << "Player seeking failed (Internal)";
    if (old_state == State::Playing) resume();
    return false;
  }

  // 4. 彻底排空解码器里的残留帧，底层的 SDL 硬件回调也要停止
  audio_renderer_.lockDevice();

  {
    std::lock_guard<std::mutex> video_lock(video_decoder_mutex_);
    video_decoder_.flush();
  }
  {
    // 与 audioDecodeLoop 形成互斥
    std::lock_guard<std::mutex> audio_lock(audio_decoder_mutex_);

    audio_decoder_.flush();

    audio_renderer_.stop();
    audio_renderer_.resetClock(target_us);
  }

  current_video_pts_ = target_us;
  last_video_pts_us_ = AV_NOPTS_VALUE;
  last_video_delay_us_ = 0;

  audio_seek_pending_ = packet_producer_.getAudioStream() != nullptr;
  video_seek_pending_ = packet_producer_.getVideoStream() != nullptr;

  audio_renderer_.unlockDevice();

  if (old_state == State::Playing) {
    // resume();
    state_ = State::Playing;  // 仅内部改状态，触发解码循环继续工作
  }

  return true;
}

void Player::audioDecodeLoop() {
  LOG_INFO << "Audio decode loop started";
  const AVStream* audio_stream = packet_producer_.getAudioStream();

  while (running_) {
    if (state_ != State::Playing) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    uint64_t generation = seek_generation_.load(std::memory_order_acquire);
    PacketPtr pkt = packet_producer_.pullAudioPacket();
    if (!pkt) {
      if (packet_producer_.isEOF()) break;  // 结束
      continue;                             // 未结束，可能是被跳转、停机给唤醒
    }

    bool pushed = false;
    {
      std::lock_guard<std::mutex> lock(audio_decoder_mutex_);
      if (!isSeekGenerationStale(generation)) {
        pushed = audio_decoder_.pushPacket(pkt);  // 跳转后重新解码
      }
    }

    if (pushed) {
      while (true) {
        FramePtr frame;
        {
          // 【锁 1】：保护从解码器取帧
          std::lock_guard<std::mutex> lock(audio_decoder_mutex_);
          if (isSeekGenerationStale(generation)) break;
          frame = audio_decoder_.pullFrame();
        }
        if (!frame) break;
        if (isSeekGenerationStale(generation)) break;

        int64_t pts_us = getFramePtsUs(frame.get(), audio_stream);
        if (shouldDropFrameBeforeSeekTarget(pts_us, true)) continue;

        bool is_ghost = false;
        {
          // 【锁 2】：保护判断 pending 和重置时钟
          std::lock_guard<std::mutex> lock(audio_decoder_mutex_);
          if (isSeekGenerationStale(generation)) {
            is_ghost = true;
          } else if (audio_seek_pending_.load(std::memory_order_acquire)) {
            audio_renderer_.resetClock(pts_us);
            audio_seek_pending_ = false;
          }
        }
        if (is_ghost) break;

        // 此时身上没有任何锁，即使被阻塞也绝对不会死锁！
        audio_renderer_.enqueueFrame(std::move(frame));
      }
    }
  }
  LOG_INFO << "Audio decode loop exited";
}

void Player::videoDecodeAndRenderLoop() {
  LOG_INFO << "Video decode & render loop started";
  const AVStream* video_stream = packet_producer_.getVideoStream();

  while (running_) {
    if (state_ != State::Playing) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    uint64_t generation = seek_generation_.load(std::memory_order_acquire);
    PacketPtr pkt = packet_producer_.pullVideoPacket();

    if (!pkt) {
      if (packet_producer_.isEOF()) {  // 由视频线程负责宣告结束和设置状态
        LOG_INFO << "Player reached EOF";
        changeState(State::Stopped);
        break;
      }
      continue;
    }

    bool pushed = false;
    {
      std::lock_guard<std::mutex> lock(video_decoder_mutex_);
      if (!isSeekGenerationStale(generation)) {
        pushed = video_decoder_.pushPacket(pkt);
      }
    }

    if (pushed) {
      while (true) {
        FramePtr frame;
        {
          std::lock_guard<std::mutex> lock(video_decoder_mutex_);
          if (isSeekGenerationStale(generation)) break;
          frame = video_decoder_.pullFrame();  // 视频帧
        }
        if (!frame) break;
        if (isSeekGenerationStale(generation)) break;

        // 1. 获取视频帧时间戳 pts_us
        int64_t pts_us = getFramePtsUs(frame.get(), video_stream);
        if (shouldDropFrameBeforeSeekTarget(pts_us, false)) continue;

        if (state_ == State::Playing && !audio_renderer_.isPlaying()) {
          audio_renderer_.play();
          LOG_INFO << "Video caught up, unpausing audio device";
        }

        // video_seek_pending_ = false;
        current_video_pts_ = pts_us;

        // 2. 获取当前帧的理论持续时间
        int64_t frame_duration_us =
            getVideoFrameDurationUs(frame.get(), video_stream);
        int64_t frame_delay_us = frame_duration_us;

        if (last_video_pts_us_ != AV_NOPTS_VALUE) {
          int64_t inferred_delay = pts_us - last_video_pts_us_;
          if (inferred_delay > 0 && inferred_delay <= AV_TIME_BASE) {
            frame_delay_us = inferred_delay;  // 计算当前帧与上一帧的时间间隔
          }
        }
        last_video_pts_us_ = pts_us;
        last_video_delay_us_ = frame_delay_us;  // 时间间隔也即上一帧持续时间
        frame_duration_us = frame_delay_us;     // 优先考虑沿用持续时间

        // --- 3. 音视频同步核心算法 ---
        if (packet_producer_.getAudioStream()) {
          int64_t audio_clock = getMasterClock();
          int64_t diff = pts_us - audio_clock;
          int64_t sync_threshold =
              std::max(AV_SYNC_THRESHOLD_MIN,
                       std::min(AV_SYNC_THRESHOLD_MAX, frame_duration_us / 2));

          if (diff < -sync_threshold) {
            // 脱节 -> 丢帧追赶
            if (!video_seek_pending_.load() && !audio_seek_pending_.load()) {
              if (diff > -30000000) {
                LOG_WARN << "Video lagging behind audio by " << -diff / 1000
                         << "ms. Dropping frame.";
              }
              // 忽略因硬件响应差异带来的大幅度脱节报警
            }
            continue;
          } else if (diff > sync_threshold &&
                     !interruptibleSleepFor(diff, generation)) {
            // 超前 -> 等待音频
            continue;
          }
        } else if (!interruptibleSleepFor(frame_duration_us, generation)) {
          // 老老实实按 frame_duration_us 持续
          continue;
        }

        renderer_.render(frame);

        if (timestamp_callback_) {
          timestamp_callback_(getCurrentTimestamp(), total_duration_s_);
        }
      }
    }
  }
  LOG_INFO << "Video decode & render loop exited";

  if (renderer_.getWindow()) {
    glfwMakeContextCurrent(nullptr);
  }
}

int64_t Player::getMasterClock() const {
  if (packet_producer_.getAudioStream()) {
    return audio_renderer_.getAudioClock();
  }
  return current_video_pts_;
}

bool Player::isSeekGenerationStale(uint64_t generation) const {
  return generation != seek_generation_.load(std::memory_order_acquire);
}

int64_t Player::getFramePtsUs(const AVFrame* frame,
                              const AVStream* stream) const {
  if (!frame || !stream) return AV_NOPTS_VALUE;

  int64_t pts = frame->best_effort_timestamp;
  if (pts == AV_NOPTS_VALUE) pts = frame->pts;
  if (pts == AV_NOPTS_VALUE) pts = frame->pkt_dts;
  if (pts == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;

  return av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
}

int64_t Player::getVideoFrameDurationUs(const AVFrame* frame,
                                        const AVStream* video_stream) const {
  int64_t duration_us = 0;
  // 1. 尝试从 AVFrame 本身获取持续时间（比如 MKV）
  if (frame && frame->duration > 0) {
    duration_us =
        av_rescale_q(frame->duration, video_stream->time_base, AV_TIME_BASE_Q);
  }
  // 2. 尝试从 AVStream 的平均帧率推算持续时间 = 1 / 平均帧率
  if (duration_us <= 0 && video_stream->avg_frame_rate.num > 0 &&
      video_stream->avg_frame_rate.den > 0) {
    duration_us =
        av_rescale_q(1, av_inv_q(video_stream->avg_frame_rate), AV_TIME_BASE_Q);
  }
  // 3. 尝试从 AVFrame 的真实帧率算，这也是容器建议的最低渲染频率
  if (duration_us <= 0 && video_stream->r_frame_rate.num > 0 &&
      video_stream->r_frame_rate.den > 0) {
    duration_us =
        av_rescale_q(1, av_inv_q(video_stream->r_frame_rate), AV_TIME_BASE_Q);
  }
  // 4. 尝试沿用上一帧的延迟时间（
  if (duration_us <= 0 && last_video_delay_us_ > 0) {
    duration_us = last_video_delay_us_;
  }
  if (duration_us <= 0) {
    duration_us = 40000;  // 兜底 25fps
  }
  return duration_us;
}

bool Player::interruptibleSleepFor(int64_t sleep_us, uint64_t generation) {
  int64_t remaining_us = std::max<int64_t>(0, sleep_us);
  while (remaining_us > 0) {
    if (!running_ || state_ != State::Playing ||
        isSeekGenerationStale(generation)) {
      return false;
    }

    int64_t slice_us = std::min(remaining_us, VIDEO_SLEEP_SLICE_US);
    std::this_thread::sleep_for(std::chrono::microseconds(slice_us));
    remaining_us -= slice_us;
  }
  return true;
}

bool Player::shouldDropFrameBeforeSeekTarget(int64_t pts_us, bool is_audio) {
  if (pts_us == AV_NOPTS_VALUE) return false;

  std::atomic<bool>& pending =
      is_audio ? audio_seek_pending_ : video_seek_pending_;
  int64_t target_us = seek_target_us_.load(std::memory_order_acquire);

  // 如果还在 pending 状态，且没追到目标，继续丢帧
  if (pending.load(std::memory_order_acquire)) {
    if (target_us != AV_NOPTS_VALUE && pts_us < target_us) {
      return true;
    }
    pending = false;  // 追上了，关掉标志
  }

  return false;
}

void Player::changeState(State new_state) {
  if (state_ == new_state) return;
  state_ = new_state;
  if (state_callback_) {
    state_callback_(new_state);
  }
}

double Player::getCurrentTimestamp() const {
  return static_cast<double>(getMasterClock()) / AV_TIME_BASE;
}

bool Player::isFinished() const {
  return state_ == State::Stopped && packet_producer_.isEOF();
}

double Player::getDuration() const { return total_duration_s_; }

GLFWwindow* Player::getWindow() const { return renderer_.getWindow(); }

void Player::setVolume(double norm) { audio_renderer_.setVolume(norm); }

double Player::getVolume() const { return audio_renderer_.getVolume(); }

}  // namespace avplayer
