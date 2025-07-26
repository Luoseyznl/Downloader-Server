#include "Downloader.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "logger.hpp"
#include "tbb_manager.hpp"
#include "timer.hpp"

namespace {

// 写入回调
size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
  std::ofstream* ofs = static_cast<std::ofstream*>(stream);
  ofs->write(static_cast<char*>(ptr), size * nmemb);
  return size * nmemb;
}

// 获取远程文件大小
size_t getRemoteFileSize(const std::string& url) {
  CURL* curl = curl_easy_init();
  double file_size = 0;
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (curl_easy_perform(curl) == CURLE_OK) {
      curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &file_size);
    }
    curl_easy_cleanup(curl);
  }
  return static_cast<size_t>(file_size);
}

}  // namespace

Downloader::Downloader() {}
Downloader::~Downloader() {}

void Downloader::startDownload(const std::string& url,
                               const std::string& location, int threadCount) {
  LOG(INFO) << "Starting download from " << url << " to " << location
            << " with "
            << (threadCount > 0 ? threadCount
                                : tbb::info::default_concurrency())
            << " threads.";

  // 获取远程文件大小
  size_t fileSize = getRemoteFileSize(url);
  if (fileSize == 0) {
    LOG(ERROR) << "Failed to get remote file size!";
    return;
  }
  LOG(INFO) << "Remote file size: " << fileSize;

  // 分片
  int totalChunks =
      threadCount > 0 ? threadCount : tbb::info::default_concurrency();
  size_t chunkSize = fileSize / totalChunks;
  std::vector<std::string> tempFiles(totalChunks);

  // 多线程分片下载
  utils::TBBManager::GetInstance().ParallelFor<int>(
      "download", 0, totalChunks, [&](int idx) {
        size_t start = idx * chunkSize;
        size_t end =
            (idx == totalChunks - 1) ? (fileSize - 1) : (start + chunkSize - 1);

        std::ostringstream oss;
        oss << location << ".part" << idx;
        std::string partFile = oss.str();
        tempFiles[idx] = partFile;

        CURL* curl = curl_easy_init();
        if (!curl) {
          LOG(ERROR) << "curl_easy_init failed for chunk " << idx;
          return;
        }
        std::ofstream ofs(partFile, std::ios::binary);
        if (!ofs) {
          LOG(ERROR) << "Failed to open part file: " << partFile;
          curl_easy_cleanup(curl);
          return;
        }
        std::string range = std::to_string(start) + "-" + std::to_string(end);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        LOG(INFO) << "Downloading chunk " << idx << " [" << range << "]";
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
          LOG(ERROR) << "Chunk " << idx
                     << " failed: " << curl_easy_strerror(res);
        } else {
          LOG(INFO) << "Chunk " << idx << " done.";
        }
        curl_easy_cleanup(curl);
        ofs.close();
      });

  // 合并分片
  std::ofstream ofs(location, std::ios::binary);
  if (!ofs) {
    LOG(ERROR) << "Failed to create output file: " << location;
    return;
  }
  for (const auto& partFile : tempFiles) {
    std::ifstream ifs(partFile, std::ios::binary);
    ofs << ifs.rdbuf();
    ifs.close();
    std::remove(partFile.c_str());
  }
  ofs.close();

  LOG(INFO) << "All chunks downloaded and merged to " << location;
}