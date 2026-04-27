#include "packet_producer.h"

#include <chrono>

#include "logger.h"

using namespace utils;

namespace avplayer {

PacketProducer::PacketProducer(size_t video_queue_size, size_t audio_queue_size)
    : video_queue_(video_queue_size), audio_queue_(audio_queue_size) {
  LOG_INFO << "PacketProducer created. Video Queue: " << video_queue_size
           << ", Audio Queue: " << audio_queue_size;
}

PacketProducer::~PacketProducer() {
  LOG_INFO << "Destroying PacketProducer";
  close();
}

bool PacketProducer::open(const std::string& filename) {
  if (!demuxer_.open(filename)) {
    LOG_ERROR << "PacketProducer failed to open demuxer for: " << filename;
    return false;
  }

  eof_ = false;
  LOG_INFO << "PacketProducer successfully opened: " << filename;
  return true;
}

// 清理资源
void PacketProducer::close() {
  stop();                // 停止读写
  demuxer_.close();      // 释放 format_ctx_
  video_queue_.clear();  // 清空残留包
  audio_queue_.clear();
  eof_ = false;
  LOG_INFO << "PacketProducer closed";
}

void PacketProducer::start() {
  if (running_) return;

  LOG_INFO << "Starting PacketProducer thread...";
  running_ = true;
  paused_ = false;
  eof_ = false;
  seek_req_ = false;

  thread_ = std::thread(&PacketProducer::produceLoop, this);  // 启动取包线程
}

void PacketProducer::stop() {
  if (!running_) return;

  LOG_INFO << "Stopping PacketProducer thread...";
  running_ = false;

  video_queue_.stop();  // 停止读写，并唤醒阻塞的读写线程
  audio_queue_.stop();

  if (thread_.joinable()) {
    thread_.join();
  }
}

void PacketProducer::pause(bool pause) {
  paused_ = pause;
  LOG_INFO << (pause ? "PacketProducer paused" : "PacketProducer resumed");
}

bool PacketProducer::seek(int64_t timestamp_us) {
  LOG_INFO << "PacketProducer seeking to " << timestamp_us << " us";

  seek_timestamp_us_ = timestamp_us;
  seek_req_ = true;

  video_queue_.clear();  // 丢弃残留包
  audio_queue_.clear();

  return true;  // 成功发出跳转请求，由 produceLoop 实施跳转
}

PacketPtr PacketProducer::pullVideoPacket() {
  PacketPtr pkt;
  video_queue_.pop(pkt);
  return pkt;
}

PacketPtr PacketProducer::pullAudioPacket() {
  PacketPtr pkt;
  audio_queue_.pop(pkt);
  return pkt;
}

void PacketProducer::produceLoop() {
  LOG_INFO << "PacketProducer background thread running";

  int video_idx = demuxer_.getStreamIndex(MediaType::Video);
  int audio_idx = demuxer_.getStreamIndex(MediaType::Audio);

  while (running_) {
    if (seek_req_) {
      LOG_INFO << "PacketProducer seeking to " << seek_timestamp_us_ << "us";

      demuxer_.seek(seek_timestamp_us_);  // 阻塞后向跳转

      video_queue_.clear();  // 再次清空，防御式编程
      audio_queue_.clear();

      eof_ = false;
      seek_req_ = false;
    }

    if (paused_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 避免空转
      continue;
    }

    PacketPtr pkt = demuxer_.pullPacket();  // 阻塞取包

    if (!pkt) {
      if (demuxer_.isEOF() && !eof_) {
        eof_ = true;
        LOG_INFO << "PacketProducer dispatching EOF signal to queues.";

        if (video_idx >= 0) video_queue_.push(nullptr);  // decoder drain
        if (audio_idx >= 0) audio_queue_.push(nullptr);
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 避免空转
      continue;
    }  // 成功取包后不必睡眠挂起，因为解码较慢，安全队列天然会阻塞取包线程

    if (pkt->stream_index == video_idx) {
      video_queue_.push(std::move(pkt));
    } else if (pkt->stream_index == audio_idx) {
      audio_queue_.push(std::move(pkt));
    } else {
      // 其他无关轨道 pkt 离开作用域会自动释放内存，不造成泄漏
    }
  }

  LOG_INFO << "PacketProducer background thread exited";
}

}  // namespace avplayer