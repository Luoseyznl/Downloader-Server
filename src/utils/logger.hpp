#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace utils {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

struct LogConfig {
  std::string logFilePath;  // 日志目录（如 "logs"）
  size_t maxFileSize;       // 单个日志文件最大字节数
  size_t maxBackupFiles;    // 最大备份文件数
  LogConfig()
      : logFilePath("logs"),
        maxFileSize(10 * 1024 * 1024),  // 10 MB
        maxBackupFiles(3) {}
};

class Logger {
 public:
  // 初始化日志系统（可选配置）
  static void initialize(const LogConfig& config = LogConfig());

  // 日志流式写入
  class LogStream {
   public:
    LogStream(LogLevel level, const char* file, const char* function, int line);
    ~LogStream();

    template <typename T>
    LogStream& operator<<(const T& msg) {
      oss_ << msg;
      return *this;
    }

   private:
    LogLevel level_;
    std::ostringstream oss_;
  };

 private:
  Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
};

}  // namespace utils

// 日志宏用法：LOG(INFO) << "message";
#define LOG(level)                                                         \
  utils::Logger::LogStream(utils::LogLevel::level, __FILE__, __FUNCTION__, \
                           __LINE__)
