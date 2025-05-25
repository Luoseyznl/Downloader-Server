#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>
#include "DownloadTask.h"
#include "../ThreadPool/ThreadPool.h"
#include "../Logger/LoggerManager.h"

class Downloader {
public:
    explicit Downloader(size_t threadPoolSize = 4);
    ~Downloader();

    // Task management
    int addTask(const std::string& url, const std::string& outputPath);
    bool removeTask(int taskId);
    std::shared_ptr<DownloadTask> getTask(int taskId);
    
    // Single task operations
    bool startTask(int taskId);
    bool pauseTask(int taskId);
    bool resumeTask(int taskId);
    bool cancelTask(int taskId);
    
    // Bulk operations
    bool startAll();
    bool pauseAll();
    bool resumeAll();
    bool cancelAll();
    
    // Status information
    size_t getTaskCount() const;
    size_t getActiveTaskCount() const;
    std::vector<int> getTaskIds() const;
    
    // Configuration (applies to new tasks)
    void setDefaultTimeout(long timeout) { defaultTimeout_ = timeout; }

private:
    ThreadPool threadPool_;
    std::unordered_map<int, std::shared_ptr<DownloadTask>> tasks_;
    std::atomic<int> nextTaskId_{0};
    std::atomic<long> defaultTimeout_{30};
    std::shared_ptr<spdlog::logger> logger_;
    
    mutable std::mutex tasksMutex_;
};