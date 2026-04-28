#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "demuxer.h"
#include "mediadefs.h"
#include "safe_queue.h"

namespace avplayer {

class PacketProducer {
 public:
  explicit PacketProducer(size_t video_queue_size = 200,
                          size_t audio_queue_size = 500)
      : video_queue_(video_queue_size), audio_queue_(audio_queue_size) {}
  ~PacketProducer() { close(); }

  PacketProducer(const PacketProducer&) = delete;
  PacketProducer& operator=(const PacketProducer&) = delete;

  bool open(const std::string& filename);
  void close();
  void start();
  void stop();
  void pause(bool pause);

  bool seek(int64_t timestamp);

  PacketPtr pullVideoPacket();  // 阻塞取包
  PacketPtr pullAudioPacket();

  const AVStream* getVideoStream() const {
    return demuxer_.getAVStream(MediaType::Video);
  }
  const AVStream* getAudioStream() const {
    return demuxer_.getAVStream(MediaType::Audio);
  }

  int64_t getDuration() const { return demuxer_.getDuration(); }

  bool isEOF() const {
    return eof_ && video_queue_.empty() && audio_queue_.empty();
  }

 private:
  void produceLoop();  // 取包 -> 入队

  Demuxer demuxer_;  // 独占式管理

  utils::SafeQueue<PacketPtr> video_queue_;
  utils::SafeQueue<PacketPtr> audio_queue_;

  std::thread thread_;

  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> eof_{false};

  std::atomic<bool> seek_req_{false};
  std::atomic<int64_t> seek_timestamp_us_{0};  //
};

}  // namespace avplayer