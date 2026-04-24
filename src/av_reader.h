#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "demuxer.h"
#include "mediadefs.h"
#include "safe_queue.h"

namespace avplayer {

/**
 * @brief 媒体数据读取器
 * 职责：
 * 1. 独占式管理 Demuxer。
 * 2. 启动一个后台线程不断 pullPacket()。
 * 3. 按照 MediaType，将 Packet 分发到对应的安全队列中。
 * 4. 为下游（Decoder）提供阻塞式的获取包裹接口。
 */
class AVReader {
 public:
  explicit AVReader(size_t video_queue_size = 200,
                    size_t audio_queue_size = 500);
  ~AVReader();

  bool open(const std::string& filename);
  void close();

  void start();
  void stop();
  void pause(bool pause);

  // 暂停线程 -> 清空双端队列 -> 调 Demuxer 跳转 -> 恢复线程
  bool seek(int64_t timestamp);

  // 阻塞等待拉取数据
  PacketPtr pullVideoPacket();
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
  void readLoop();  // 核心物流分发线程

  Demuxer demuxer_;

  utils::SafeQueue<PacketPtr> video_queue_;
  utils::SafeQueue<PacketPtr> audio_queue_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> eof_{false};
};

}  // namespace avplayer