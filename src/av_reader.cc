#include "av_reader.h"

#include <chrono>

#include "logger.h"

using namespace utils;

namespace avplayer {

AVReader::AVReader(size_t video_queue_size, size_t audio_queue_size)
    : video_queue_(video_queue_size), audio_queue_(audio_queue_size) {
  LOG_INFO << "AVReader created. Video Queue: " << video_queue_size
           << ", Audio Queue: " << audio_queue_size;
}

AVReader::~AVReader() {
  LOG_INFO << "Destroying AVReader";
  close();
}

bool AVReader::open(const std::string& filename) {
  if (!demuxer_.open(filename)) {
    LOG_ERROR << "AVReader failed to open demuxer for: " << filename;
    return false;
  }

  eof_ = false;
  LOG_INFO << "AVReader successfully opened: " << filename;
  return true;
}

void AVReader::close() {
  stop();
  demuxer_.close();
  video_queue_.clear();
  audio_queue_.clear();
  eof_ = false;
  LOG_INFO << "AVReader closed";
}

void AVReader::start() {
  if (running_) return;

  LOG_INFO << "Starting AVReader thread...";
  running_ = true;
  paused_ = false;
  eof_ = false;

  // 启动后台物流分发线程
  thread_ = std::thread(&AVReader::readLoop, this);
}

void AVReader::stop() {
  if (!running_) return;

  LOG_INFO << "Stopping AVReader thread...";
  running_ = false;

  // 解除队列的阻塞状态
  video_queue_.stop();
  audio_queue_.stop();

  if (thread_.joinable()) {
    thread_.join();
  }
}

void AVReader::pause(bool pause) {
  paused_ = pause;
  LOG_INFO << (pause ? "AVReader paused" : "AVReader resumed");
}

bool AVReader::seek(int64_t timestamp) {
  LOG_INFO << "AVReader seeking to " << timestamp << " us";

  paused_ = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  video_queue_.clear();
  audio_queue_.clear();

  bool ret = demuxer_.seek(timestamp);

  eof_ = false;
  paused_ = false;

  return ret;
}

PacketPtr AVReader::pullVideoPacket() {
  PacketPtr pkt;
  video_queue_.pop(pkt);  // 阻塞等待直到有包，或者队列被 stop()
  return pkt;
}

PacketPtr AVReader::pullAudioPacket() {
  PacketPtr pkt;
  audio_queue_.pop(pkt);  // 阻塞等待
  return pkt;
}

void AVReader::readLoop() {
  LOG_INFO << "AVReader background thread running";

  int video_idx = demuxer_.getStreamIndex(MediaType::Video);
  int audio_idx = demuxer_.getStreamIndex(MediaType::Audio);

  while (running_) {
    if (paused_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    PacketPtr pkt = demuxer_.pullPacket();

    if (!pkt) {
      if (demuxer_.isEOF() && !eof_) {
        eof_ = true;
        LOG_INFO << "AVReader reached EOF. Dispatching EOF signal to queues.";

        // Decoder 收到 nullptr 就知道该 flush 内部残留帧了！
        if (video_idx >= 0) video_queue_.push(nullptr);
        if (audio_idx >= 0) audio_queue_.push(nullptr);
      }
      // 读到末尾了，休息一下，避免空转 CPU
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (pkt->stream_index == video_idx) {
      video_queue_.push(std::move(pkt));
    } else if (pkt->stream_index == audio_idx) {
      audio_queue_.push(std::move(pkt));
    } else {
      // 忽略其他无关轨道（比如字幕流、封面图片等）
      // pkt 离开作用域会自动释放内存，不造成泄漏
    }
  }

  LOG_INFO << "AVReader background thread exited";
}

}  // namespace avplayer