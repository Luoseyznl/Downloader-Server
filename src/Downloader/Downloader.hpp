#ifndef DOWNLOADER_HPP_
#define DOWNLOADER_HPP_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "logger.hpp"

class Downloader {
 public:
  Downloader();
  ~Downloader();

  void startDownload(const std::string& user, const std::string& location,
                     int threadCount = 0);
  void cancelDownload(int taskId);
  void listDownloads() const;

 private:
  struct DownloadTask {
    int id;
    std::string user;
    std::string ip;
    std::string location;
    bool isActive;
    // Additional fields for progress tracking, etc.
  };

  int nextTaskId_;
  std::unordered_map<int, DownloadTask> tasks_;
  std::shared_ptr<utils::Logger> logger_;

  void reportProgress(int taskId, size_t downloaded, size_t total);
  void handleDownload(const DownloadTask& task);
};

#endif  // DOWNLOADER_HPP_