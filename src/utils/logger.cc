#include "logger.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

namespace utils {

// \033(\e) 表示终端指令 [...m 设置为编译期常量，提升性能
struct Color {
  static constexpr const char* RESET = "\033[0m";     // 恢复默认颜色
  static constexpr const char* RED = "\033[31m";      // 31 红色代码
  static constexpr const char* GREEN = "\033[32m";    // 32 绿色代码
  static constexpr const char* YELLOW = "\033[33m";   // 33 黄色代码
  static constexpr const char* MAGENTA = "\033[35m";  // 35 洋红色代码
};

// 返回只读数据段里的静态字符串常量指针
static const char* getLevelStr(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG:
      return "DEBUG";
    case LogLevel::INFO:
      return "INFO ";
    case LogLevel::WARN:
      return "WARN ";
    case LogLevel::ERROR:
      return "ERROR";
    case LogLevel::FATAL:
      return "FATAL";
    default:
      return "UNKWN";
  }
}

static const char* getLevelColor(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG:
      return Color::MAGENTA;
    case LogLevel::INFO:
      return Color::GREEN;
    case LogLevel::WARN:
      return Color::YELLOW;
    case LogLevel::ERROR:
      return Color::RED;
    case LogLevel::FATAL:
      return Color::RED;
    default:
      return Color::RESET;
  }
}

static const char* getFileName(const char* filePath) {
  const char* fileName = filePath;
  for (const char* p = filePath; *p; ++p) {
    if (*p == '/' || *p == '\\') fileName = p + 1;
  }
  return fileName;
}

// 普通 static 成员变量需要在类外初始化，分配实际内存空间
LogLevel Logger::globalLevel_ = LogLevel::INFO;

Logger& Logger::getInstance() {
  static Logger instance;
  return instance;
}

// init(config) 触发单例实例化，并启动后台线程开始写入日志
void Logger::init(const LogConfig& config) { getInstance().initLogger(config); }

void Logger::setGlobalLevel(LogLevel level) { globalLevel_ = level; }
LogLevel Logger::getGlobalLevel() { return globalLevel_; }

Logger::LogStream::LogStream(LogLevel level, const char* file,
                             const char* function, int line)
    : level_(level), file_(file), function_(function), line_(line) {}

Logger::LogStream::~LogStream() {
  if (level_ >= Logger::getGlobalLevel()) {
    Logger::log(level_, file_, function_, line_, stream_.str());
  }
}

void Logger::log(LogLevel level, const char* file, const char* function,
                 int line, const std::string& message) {
  auto now = std::chrono::system_clock::now();  // 获取系统时钟 system_clock
  auto now_time_t = std::chrono::system_clock::to_time_t(now);  // std::time_t
  auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now)
                    .time_since_epoch()
                    .count() %
                1000;  // 获取 0-999 ms 值

  std::tm now_tm;
  localtime_r(&now_time_t, &now_tm);  // Linux/Unix 线程安全的时间获取

  // 格式化日志头部 [2026-04-21 15:30:45.123] [INFO ] [main.cc:42]
  char header_buf[128];
  snprintf(header_buf, sizeof(header_buf),
           "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%s:%d] ",
           now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday,
           now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec, (int)now_ms,
           getLevelStr(level), getFileName(file), line);

  std::string final_msg = std::string(header_buf) + message + "\n";

  // 终端彩色输出
  std::cout << getLevelColor(level) << final_msg << Color::RESET;
  //   if (level >= LogLevel::ERROR)
  std::cout.flush();  // 立即刷新输出缓冲

  getInstance().writeToFile(final_msg);
}

void Logger::initLogger(const LogConfig& config) {
  config_ = config;
  std::filesystem::create_directories(config_.log_dir);
  current_file_path_ = getNewLogFilePath();
  openLogFile();

  if (config_.async_mode) {
    write_thread_ = std::thread(&Logger::processLogQueue, this);
  }
}

Logger::~Logger() {
  if (config_.async_mode) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      stop_thread_ = true;
    }
    queue_cv_.notify_one();
    if (write_thread_.joinable()) write_thread_.join();
  }
}

void Logger::writeToFile(const std::string& message) {
  if (config_.async_mode) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    log_queue_.push(message);
    queue_cv_.notify_one();
  } else {
    std::lock_guard<std::mutex> lock(
        queue_mutex_);  // 同步写入也会有多线程同时写日志，需要保护 log_file_
    writeLogToFile(message);
  }
}

void Logger::processLogQueue() {
  std::queue<std::string> local_queue;

  while (true) {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock,
                     [this] { return !log_queue_.empty() || stop_thread_; });

      if (stop_thread_ && log_queue_.empty()) break;

      local_queue.swap(log_queue_);  // 换出（效率更高）
    }

    while (!local_queue.empty()) {
      writeLogToFile(local_queue.front());
      local_queue.pop();
    }
  }
}

void Logger::writeLogToFile(const std::string& message) {
  rotateLogFileIfNeeded();
  if (log_file_.is_open()) {
    log_file_ << message;
    log_file_.flush();
  }
}

void Logger::rotateLogFileIfNeeded() {
  if (!std::filesystem::exists(current_file_path_)) {
    openLogFile();
    return;
  }
  if (std::filesystem::file_size(current_file_path_) >= config_.max_file_size) {
    current_file_path_ = getNewLogFilePath();
    openLogFile();
    cleanOldLogFiles();
  }
}

void Logger::openLogFile() {
  if (log_file_.is_open()) log_file_.close();
  log_file_.open(current_file_path_, std::ios::app);
}

std::string Logger::getNewLogFilePath() {
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << config_.log_dir << "/avplayer_"
     << std::put_time(std::localtime(&now_time_t), "%Y%m%d_%H%M%S") << ".log";
  return ss.str();
}

void Logger::cleanOldLogFiles() {
  std::vector<std::filesystem::path> log_files;
  for (const auto& entry :
       std::filesystem::directory_iterator(config_.log_dir)) {
    if (entry.path().extension() == ".log") log_files.push_back(entry.path());
  }

  if (log_files.size() > config_.max_files) {
    std::sort(log_files.begin(), log_files.end());  // 系统调用不一定按序遍历
    for (size_t i = 0; i < log_files.size() - config_.max_files; ++i) {
      std::filesystem::remove(log_files[i]);
    }
  }
}

}  // namespace utils
