#include "logger.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace utils {

namespace {
std::mutex log_mutex;
std::ofstream log_file;
std::string log_dir = "logs";
std::string log_file_path;
size_t max_file_size = 10 * 1024 * 1024;  // 10MB
size_t max_backup_files = 3;

const char* getLevelStr(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG:
      return "DEBUG";
    case LogLevel::INFO:
      return "INFO";
    case LogLevel::WARN:
      return "WARN";
    case LogLevel::ERROR:
      return "ERROR";
    case LogLevel::FATAL:
      return "FATAL";
    default:
      return "UNKNOWN";
  }
}

std::string getCurrentTime() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  std::tm tm;
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0')
      << std::setw(3) << ms.count();
  return oss.str();
}

void rotateLogsIfNeeded() {
  if (!log_file_path.empty() && std::filesystem::exists(log_file_path) &&
      std::filesystem::file_size(log_file_path) >= max_file_size) {
    log_file.close();
    // Rotate old logs
    for (int i = max_backup_files - 1; i >= 0; --i) {
      std::string old_name =
          log_file_path + (i == 0 ? "" : ("." + std::to_string(i)));
      std::string new_name = log_file_path + "." + std::to_string(i + 1);
      if (std::filesystem::exists(old_name)) {
        std::filesystem::rename(old_name, new_name);
      }
    }
    log_file.open(log_file_path, std::ios::trunc);
  }
}

void ensureLogDir() {
  if (!std::filesystem::exists(log_dir)) {
    std::filesystem::create_directories(log_dir);
  }
}

void openLogFile() {
  ensureLogDir();
  log_file_path = log_dir + "/downloader.log";
  log_file.open(log_file_path, std::ios::app);
  if (!log_file.is_open()) {
    std::cerr << "Failed to open log file: " << log_file_path << std::endl;
  }
}
}  // namespace

void Logger::initialize(const LogConfig& config) {
  std::lock_guard<std::mutex> lock(log_mutex);
  log_dir = config.logFilePath.empty() ? "logs" : config.logFilePath;
  max_file_size = config.maxFileSize ? config.maxFileSize : 10 * 1024 * 1024;
  max_backup_files = config.maxBackupFiles ? config.maxBackupFiles : 3;
  openLogFile();
}

Logger::LogStream::LogStream(LogLevel level, const char* file, const char* func,
                             int line)
    : level_(level), oss_() {
  oss_ << "[" << getLevelStr(level) << "] " << getCurrentTime() << " " << file
       << ":" << line << " " << func << ": ";
}

Logger::LogStream::~LogStream() {
  oss_ << "\n";
  std::string msg = oss_.str();
  {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (!log_file.is_open()) openLogFile();
    rotateLogsIfNeeded();
    std::cout << msg;
    if (log_file.is_open()) log_file << msg, log_file.flush();
  }
}

}  // namespace utils