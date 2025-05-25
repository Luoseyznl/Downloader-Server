#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <memory>
#include "../Logger/LoggerManager.h"

typedef void CURL;
struct curl_slist;

class DownloadTask {
public:
    enum class Status {
        Idle,
        Downloading,
        Paused,
        Completed,
        Failed,
        Cancelled
    };

    using ProgressCallback = std::function<void(size_t downloadedBytes, size_t totalBytes)>;
    using WriteCallback = std::function<size_t(const char* data, size_t length)>;

    DownloadTask(const std::string& url, const std::string& outputPath);
    ~DownloadTask();

    // Task configuration
    void setRangeStart(size_t start) { rangeStart_ = start; }
    void setRangeEnd(size_t end) { rangeEnd_ = end; }
    void setTimeout(long timeout) { timeout_ = timeout; }
    void setProgressCallback(ProgressCallback callback) { progressCallback_ = callback; }
    void setWriteCallback(WriteCallback callback) { writeCallback_ = callback; }

    // Task control
    bool start();
    bool pause();
    bool resume();
    bool cancel();

    // Status getters
    Status getStatus() const { return status_; }
    size_t getDownloadedSize() const { return downloadedSize_; }
    size_t getTotalSize() const { return totalSize_; }
    double getProgress() const;
    std::string getErrorMessage() const { return errorMessage_; }
    const std::string& getUrl() const { return url_; }
    const std::string& getOutputPath() const { return outputPath_; }

private:
    // CURL callbacks
    static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static int curlProgressCallback(void* clientp, double dltotal, double dlnow, double ultotal, double ulnow);
    
    // Instance methods
    size_t onWriteData(const char* data, size_t length);
    int onProgress(double dltotal, double dlnow);
    bool setupCurl();
    void cleanup();
    bool performDownload();
    
    // Task parameters
    std::string url_;
    std::string outputPath_;
    std::atomic<size_t> rangeStart_{0};
    std::atomic<size_t> rangeEnd_{0};
    std::atomic<long> timeout_{30};
    
    // Task state
    std::atomic<Status> status_{Status::Idle};
    std::atomic<size_t> downloadedSize_{0};
    std::atomic<size_t> totalSize_{0};
    std::atomic<bool> cancelRequested_{false};
    std::string errorMessage_;
    
    // Callbacks
    ProgressCallback progressCallback_;
    WriteCallback writeCallback_;
    
    // CURL resources
    CURL* curlHandle_{nullptr};
    curl_slist* headerList_{nullptr};
    
    // Logger
    std::shared_ptr<spdlog::logger> logger_;
};
