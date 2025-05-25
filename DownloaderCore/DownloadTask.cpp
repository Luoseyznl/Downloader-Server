#include "DownloadTask.h"
#include <curl/curl.h>
#include <fstream>
#include <iostream>

DownloadTask::DownloadTask(const std::string& url, const std::string& outputPath)
    : url_(url), outputPath_(outputPath) {
    // Initialize logger
    logger_ = LoggerManager::getInstance().getLogger("DownloadTask");
    logger_->info("Created download task for URL: {}", url_);
}

DownloadTask::~DownloadTask() {
    // Make sure we cancel any active operations and cleanup resources
    cancel();
    cleanup();
}

double DownloadTask::getProgress() const {
    if (totalSize_ == 0) {
        return 0.0;
    }
    return static_cast<double>(downloadedSize_) / totalSize_ * 100.0;
}

bool DownloadTask::start() {
    if (status_ != Status::Idle && status_ != Status::Failed) {
        logger_->warn("Cannot start download: task is already in progress or completed");
        return false;
    }

    if (!setupCurl()) {
        logger_->error("Failed to setup curl for URL: {}", url_);
        return false;
    }

    cancelRequested_ = false;
    errorMessage_.clear();

    return performDownload();
}

bool DownloadTask::pause() {
    if (status_ != Status::Downloading) {
        logger_->warn("Cannot pause: task is not downloading");
        return false;
    }
    
    logger_->info("Pausing download: {}", url_);
    cancelRequested_ = true;
    status_ = Status::Paused;
    return true;
}

bool DownloadTask::resume() {
    if (status_ != Status::Paused) {
        logger_->warn("Cannot resume: task is not paused");
        return false;
    }
    
    logger_->info("Resuming download from byte {}: {}", downloadedSize_.load(), url_);
    
    // Set the range start to the current downloaded size for resuming
    rangeStart_.store(downloadedSize_.load());
    cancelRequested_ = false;
    
    return start();
}

bool DownloadTask::cancel() {
    logger_->info("Cancelling download: {}", url_);
    cancelRequested_ = true;
    status_ = Status::Cancelled;
    return true;
}

bool DownloadTask::setupCurl() {
    // Clean up any existing CURL handle
    cleanup();
    
    // Initialize a new CURL handle
    curlHandle_ = curl_easy_init();
    if (!curlHandle_) {
        logger_->error("Failed to initialize curl");
        return false;
    }
    
    // Set basic CURL options
    curl_easy_setopt(curlHandle_, CURLOPT_URL, url_.c_str());   // Set the URL
    curl_easy_setopt(curlHandle_, CURLOPT_FOLLOWLOCATION, 1L);  // Follow redirects
    curl_easy_setopt(curlHandle_, CURLOPT_TIMEOUT, timeout_.load());   // Set timeout
    curl_easy_setopt(curlHandle_, CURLOPT_CONNECTTIMEOUT, timeout_.load());
    curl_easy_setopt(curlHandle_, CURLOPT_BUFFERSIZE, 1024L);
    curl_easy_setopt(curlHandle_, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3");
    
    // Set up callbacks
    curl_easy_setopt(curlHandle_, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curlHandle_, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curlHandle_, CURLOPT_NOPROGRESS, 0L);  // Enable progress meter
    curl_easy_setopt(curlHandle_, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curlHandle_, CURLOPT_XFERINFODATA, this);
    
    // If we're resuming, set the appropriate range
    if (rangeStart_ > 0) {
        std::string range = std::to_string(rangeStart_) + "-";
        if (rangeEnd_ > 0 && rangeEnd_ > rangeStart_) {
            range += std::to_string(rangeEnd_);
        }
        logger_->info("Setting download range: {}", range);
        curl_easy_setopt(curlHandle_, CURLOPT_RANGE, range.c_str());
    }
    
    return true;
}

void DownloadTask::cleanup() {
    if (headerList_) {
        curl_slist_free_all(headerList_);
        headerList_ = nullptr;
    }
    
    if (curlHandle_) {
        curl_easy_cleanup(curlHandle_);
        curlHandle_ = nullptr;
    }
}

bool DownloadTask::performDownload() {
    if (!curlHandle_) {
        logger_->error("Cannot perform download: CURL handle is not initialized");
        status_ = Status::Failed;
        errorMessage_ = "CURL handle is not initialized";
        return false;
    }
    
    status_ = Status::Downloading;
    logger_->info("Starting download: {}", url_);
    
    CURLcode res = curl_easy_perform(curlHandle_);
    if (res != CURLE_OK) {
        logger_->error("CURL error: {}", curl_easy_strerror(res));
        status_ = Status::Failed;
        errorMessage_ = curl_easy_strerror(res);
        return false;
    }

    if (cancelRequested_) {
        return false;
    }

    long responseCode;
    curl_easy_getinfo(curlHandle_, CURLINFO_RESPONSE_CODE, &responseCode);
    if (responseCode != 200 && responseCode != 206) {
        logger_->error("HTTP error: {}", responseCode);
        status_ = Status::Failed;
        errorMessage_ = "HTTP error: " + std::to_string(responseCode);
        return false;
    }
    status_ = Status::Completed;
    logger_->info("Download completed: {}", url_);
    return true;
}

// Static CURL callbacks
size_t DownloadTask::curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* task = static_cast<DownloadTask*>(userdata);
    return task->onWriteData(ptr, size * nmemb);
}

int DownloadTask::curlProgressCallback(void* clientp, double dltotal, double dlnow, double ultotal [[maybe_unused]], double ulnow [[maybe_unused]]) {
    auto* task = static_cast<DownloadTask*>(clientp);
    return task->onProgress(dltotal, dlnow);
}

// Instance callback implementations
size_t DownloadTask::onWriteData(const char* data, size_t length) {
    if (cancelRequested_) {
        logger_->info("Download cancelled during write operation");
        return 0; // Tells CURL to abort
    }
    
    try {
        static std::ofstream file;
        
        if (!file.is_open()) {
            // Open file in appropriate mode (append for resuming or truncate for new download)
            std::ios_base::openmode mode = std::ios::binary;
            mode |= (rangeStart_ > 0) ? std::ios::app : std::ios::trunc;
            
            file.open(outputPath_, mode);
            if (!file.is_open()) {
                logger_->error("Failed to open output file: {}", outputPath_);
                return 0; // Tells CURL to abort
            }
            
            logger_->debug("Opened output file: {} (mode: {})", 
                outputPath_, (rangeStart_ > 0) ? "append" : "truncate");
        }
        
        file.write(data, length);
        if (!file.good()) {
            logger_->error("Failed to write to output file: {}", outputPath_);
            return 0;
        }
        
        downloadedSize_ += length;
    } catch (const std::exception& ex) {
        logger_->error("Exception during file write: {}", ex.what());
        return 0;
    }
    
    return length;
}

int DownloadTask::onProgress(double dltotal, double dlnow) {
    if (cancelRequested_) {
        logger_->info("Download cancelled during progress update");
        return 1; // Non-zero return tells CURL to abort
    }
    
    // Update progress stats
    totalSize_ = rangeStart_ + static_cast<size_t>(dltotal);
    downloadedSize_ = rangeStart_ + static_cast<size_t>(dlnow);
    
    // Call the user's progress callback if set
    if (progressCallback_) {
        progressCallback_(downloadedSize_, totalSize_);
    }
    
    // Log progress at reasonable intervals (every 10%)
    static int lastProgressPercent = -1;
    int currentProgressPercent = static_cast<int>(getProgress());
    
    if (currentProgressPercent / 10 > lastProgressPercent / 10) {
        lastProgressPercent = currentProgressPercent;
        logger_->info("Download progress for {}: {}% ({}/{} bytes)",
                     url_, currentProgressPercent, downloadedSize_.load(), totalSize_.load());
    }
    
    return 0; // Continue download
}
