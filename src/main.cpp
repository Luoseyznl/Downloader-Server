#include <gflags/gflags.h>

#include <iostream>

#include "Downloader/Downloader.hpp"
#include "utils/logger.hpp"

DEFINE_int32(download_threads, 0,
             "Number of download threads (0 for max concurrency)");
DEFINE_string(custom_tbb_parallel_control, "",
              "TBB arena concurrency control, e.g. arena1:4,arena2:8");

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc != 3) {
    std::cerr << "Usage: " << argv[0]
              << " <user@url> <location> [--download_threads=N]" << std::endl;
    return 1;
  }

  // 日志初始化（可选：可自定义路径/大小/备份数）
  utils::LogConfig logCfg;
  logCfg.logFilePath = "logs";
  logCfg.maxFileSize = 10 * 1024 * 1024;
  logCfg.maxBackupFiles = 3;
  utils::Logger::initialize(logCfg);

  std::string userUrl = argv[1];
  std::string location = argv[2];

  Downloader downloader;
  downloader.startDownload(userUrl, location, FLAGS_download_threads);

  return 0;
}