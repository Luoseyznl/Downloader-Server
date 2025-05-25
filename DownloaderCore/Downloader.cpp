#include "Downloader.h"

Downloader::Downloader(size_t threadPoolSize) 
    : threadPool_(threadPoolSize) {
    logger_ = LoggerManager::getInstance().getLogger("Downloader");
    logger_->info("Downloader initialized with thread pool size: {}", threadPoolSize);
}

Downloader::~Downloader() {
    cancelAll();
}

int Downloader::addTask(const std::string& url, const std::string& outputPath) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    int taskId = nextTaskId_++;
    
    auto task = std::make_shared<DownloadTask>(url, outputPath);
    task->setTimeout(defaultTimeout_);
    
    tasks_[taskId] = task;
    logger_->info("Added download task with ID {}: {}", taskId, url);
    
    return taskId;
}

bool Downloader::removeTask(int taskId) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        logger_->warn("Attempt to remove non-existent task ID: {}", taskId);
        return false;
    }
    
    // Cancel the task before removing it
    auto task = it->second;
    task->cancel();
    tasks_.erase(it);
    
    logger_->info("Removed task with ID {}: {}", taskId, task->getUrl());
    return true;
}

std::shared_ptr<DownloadTask> Downloader::getTask(int taskId) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        return nullptr;
    }
    return it->second;
}

bool Downloader::startTask(int taskId) {
    auto task = getTask(taskId);
    if (!task) {
        logger_->warn("Attempt to start non-existent task ID: {}", taskId);
        return false;
    }
    
    logger_->info("Starting task ID {}: {}", taskId, task->getUrl());
    threadPool_.enqueue([task]() {
        task->start();
    });
    
    return true;
}

bool Downloader::pauseTask(int taskId) {
    auto task = getTask(taskId);
    if (!task) {
        logger_->warn("Attempt to pause non-existent task ID: {}", taskId);
        return false;
    }
    
    logger_->info("Pausing task ID {}: {}", taskId, task->getUrl());
    return task->pause();
}

bool Downloader::resumeTask(int taskId) {
    auto task = getTask(taskId);
    if (!task) {
        logger_->warn("Attempt to resume non-existent task ID: {}", taskId);
        return false;
    }
    
    logger_->info("Resuming task ID {}: {}", taskId, task->getUrl());
    threadPool_.enqueue([task]() {
        task->resume();
    });
    
    return true;
}

bool Downloader::cancelTask(int taskId) {
    auto task = getTask(taskId);
    if (!task) {
        logger_->warn("Attempt to cancel non-existent task ID: {}", taskId);
        return false;
    }
    
    logger_->info("Cancelling task ID {}: {}", taskId, task->getUrl());
    return task->cancel();
}

bool Downloader::startAll() {
    std::vector<int> taskIds;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        logger_->info("Starting all tasks");
        
        for (const auto& pair : tasks_) {
            taskIds.push_back(pair.first);
        }
    }
    
    for (int taskId : taskIds) {
        startTask(taskId);
    }
    
    return true;
}

bool Downloader::pauseAll() {
    std::vector<int> taskIds;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        logger_->info("Pausing all tasks");
        
        for (const auto& pair : tasks_) {
            taskIds.push_back(pair.first);
        }
    }
    
    for (int taskId : taskIds) {
        pauseTask(taskId);
    }
    
    return true;
}

bool Downloader::resumeAll() {
    std::vector<int> taskIds;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        logger_->info("Resuming all tasks");
        
        for (const auto& pair : tasks_) {
            taskIds.push_back(pair.first);
        }
    }
    
    for (int taskId : taskIds) {
        resumeTask(taskId);
    }
    
    return true;
}

bool Downloader::cancelAll() {
    std::vector<int> taskIds;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        logger_->info("Cancelling all tasks");
        
        for (const auto& pair : tasks_) {
            taskIds.push_back(pair.first);
        }
    }
    
    for (int taskId : taskIds) {
        cancelTask(taskId);
    }
    
    return true;
}

size_t Downloader::getTaskCount() const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    return tasks_.size();
}

size_t Downloader::getActiveTaskCount() const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    size_t count = 0;
    
    for (const auto& pair : tasks_) {
        auto status = pair.second->getStatus();
        if (status == DownloadTask::Status::Downloading || 
            status == DownloadTask::Status::Paused) {
            count++;
        }
    }
    
    return count;
}

std::vector<int> Downloader::getTaskIds() const {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    std::vector<int> taskIds;
    taskIds.reserve(tasks_.size());
    
    for (const auto& pair : tasks_) {
        taskIds.push_back(pair.first);
    }
    
    return taskIds;
}