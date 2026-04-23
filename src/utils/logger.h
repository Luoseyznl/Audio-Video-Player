#pragma once

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

namespace utils {

// 强枚举类型
enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

struct LogConfig {
  std::string log_dir;
  size_t max_file_size;  // 日志轮转归档
  size_t max_files;      // 日志滚动保留
  bool async_mode;       // 磁盘写入模式

  LogConfig()
      : log_dir("logs"),
        max_file_size(10 * 1024 * 1024),
        max_files(10),
        async_mode(true) {}
};

class Logger {
 public:
  // 开放给宏调用的静态接口
  static void init(const LogConfig& config = LogConfig());
  static void setGlobalLevel(LogLevel level);
  static LogLevel getGlobalLevel();
  static void log(LogLevel level, const char* file, const char* function,
                  int line, const std::string& message);  // 后端写入

  // LogStream 对象负责拼接字符串，并在析构时调用 Logger::log() 写入日志文件
  class LogStream {
   public:
    LogStream(LogLevel level, const char* file, const char* function, int line);
    ~LogStream();

    template <typename T>
    LogStream& operator<<(const T& value) {
      if (level_ >= Logger::getGlobalLevel()) {
        stream_ << value;
      }
      return *this;
    }

   private:
    LogLevel level_;
    const char* file_;
    const char* function_;
    int line_;
    std::ostringstream stream_;
  };

 private:
  Logger() = default;
  ~Logger();

  // 单例：返回自身对象的类方法 getInstance()
  static Logger& getInstance();  // 私有：强制使用静态接口调用

  void initLogger(const LogConfig& config);
  void writeToFile(const std::string& message);
  void processLogQueue();
  void writeLogToFile(const std::string& message);
  void rotateLogFileIfNeeded();
  void cleanOldLogFiles();
  std::string getNewLogFilePath();
  void openLogFile();

  static LogLevel globalLevel_;
  LogConfig config_;

  // 写线程负责将日志异步写入日志文件
  std::queue<std::string> log_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::thread write_thread_;
  bool stop_thread_ = false;

  // 文件 I/O
  std::ofstream log_file_;
  std::string current_file_path_;
};

}  // namespace utils

/**
 * 便捷宏：利用临时对象的析构函数，触发异步写入
 * __FILE__: 当前源代码文件的路径（包括文件名）
 * __FUNCTION__: 当前代码所在函数名
 * __LINE__: 当前代码所在行号
 */
#define LOG_DEBUG                                                          \
  utils::Logger::LogStream(utils::LogLevel::DEBUG, __FILE__, __FUNCTION__, \
                           __LINE__)
#define LOG_INFO                                                          \
  utils::Logger::LogStream(utils::LogLevel::INFO, __FILE__, __FUNCTION__, \
                           __LINE__)
#define LOG_WARN                                                          \
  utils::Logger::LogStream(utils::LogLevel::WARN, __FILE__, __FUNCTION__, \
                           __LINE__)
#define LOG_ERROR                                                          \
  utils::Logger::LogStream(utils::LogLevel::ERROR, __FILE__, __FUNCTION__, \
                           __LINE__)
#define LOG_FATAL                                                          \
  utils::Logger::LogStream(utils::LogLevel::FATAL, __FILE__, __FUNCTION__, \
                           __LINE__)
