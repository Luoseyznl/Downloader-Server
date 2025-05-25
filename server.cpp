#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>

#include "DownloaderCore/Downloader.h"
#include "Logger/LoggerManager.h"
#include "ThreadPool/ThreadPool.h"

class DownloadServer {
public:
    DownloadServer(int port = 10280, size_t threadPoolSize = 4)
        : port_(port), running_(false), threadPool_(threadPoolSize) {
        // Initialize logger
        logger_ = LoggerManager::getInstance().getLogger("DownloadServer");
        logger_->info("Initializing download server on port {} with thread pool size {}", port_, threadPoolSize);

        // Initialize downloader with half of the threads (reserving some for client handling)
        size_t downloaderThreads = std::max(size_t(1), threadPoolSize / 2);
        downloader_ = std::make_unique<Downloader>(downloaderThreads);

        // Register command handlers
        registerCommands();
    }

    ~DownloadServer() {
        stop();
    }

    bool start() {
        if (running_) {
            logger_->warn("Server already running");
            return false;
        }

        // Create server socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            logger_->error("Failed to create socket: {}", strerror(errno));
            return false;
        }

        // Set socket options
        int opt = 1;
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            logger_->error("Failed to set socket options: {}", strerror(errno));
            close(server_fd_);
            return false;
        }

        // Bind socket to port
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);

        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            logger_->error("Failed to bind to port {}: {}", port_, strerror(errno));
            close(server_fd_);
            return false;
        }

        // Listen for connections
        if (listen(server_fd_, 5) < 0) {
            logger_->error("Listen failed: {}", strerror(errno));
            close(server_fd_);
            return false;
        }

        // Start server thread
        running_ = true;
        server_thread_ = std::thread(&DownloadServer::serverLoop, this);
        logger_->info("Server started on port {}", port_);

        return true;
    }

    void stop() {
        if (!running_) {
            return;
        }

        logger_->info("Stopping server...");
        running_ = false;

        // Close server socket to interrupt accept()
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }

        // Wait for server thread to finish
        if (server_thread_.joinable()) {
            server_thread_.join();
        }

        logger_->info("Server stopped");
    }

private:
    void serverLoop() {
        logger_->info("Server loop started");

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (running_) {
            // Accept connection
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (running_) {
                    logger_->error("Accept failed: {}", strerror(errno));
                }
                continue;
            }

            // Handle client using the thread pool instead of creating a new thread
            threadPool_.enqueue([this, client_fd]() {
                this->handleClient(client_fd);
                });
        }

        logger_->info("Server loop ended");
    }

    void handleClient(int client_fd) {
        logger_->info("New client connected (handled by thread pool)");

        // Create progress reporter for this client
        auto progressReporter = [this, client_fd](int taskId, size_t downloaded, size_t total) {
            std::string progress;
            if (total > 0) {
                int percentage = static_cast<int>((downloaded * 100) / total);
                progress = std::to_string(taskId) + ":" + std::to_string(percentage) + "%";
                sendToClient(client_fd, "PROGRESS " + progress);
            }
            };

        char buffer[1024];
        while (running_) {
            // Receive command from client
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

            if (bytes_read <= 0) {
                // Connection closed or error
                break;
            }

            // Process command
            std::string command(buffer, bytes_read);
            command.erase(command.find_last_not_of("\r\n") + 1); // Trim newlines

            logger_->debug("Received command: '{}'", command);

            // Process the command and get response
            std::string response = processCommand(command, progressReporter);

            // Send response back to client
            sendToClient(client_fd, response);
        }

        close(client_fd);
        logger_->info("Client disconnected");
    }

    void sendToClient(int client_fd, const std::string& message) {
        std::string response = message + "\r\n";
        send(client_fd, response.c_str(), response.length(), 0);
    }

    std::string processCommand(const std::string& command, const std::function<void(int, size_t, size_t)>& progressReporter) {
        std::istringstream iss(command);
        std::string cmd;
        iss >> cmd;

        if (cmd.empty()) {
            return "ERROR Empty command";
        }

        // Convert command to uppercase for case-insensitive comparison
        for (auto& c : cmd) {
            c = toupper(c);
        }

        auto it = command_handlers_.find(cmd);
        if (it != command_handlers_.end()) {
            try {
                return it->second(iss, progressReporter);
            } catch (const std::exception& e) {
                logger_->error("Error processing command '{}': {}", command, e.what());
                return "ERROR " + std::string(e.what());
            }
        }

        logger_->warn("Unknown command: {}", cmd);
        return "ERROR Unknown command: " + cmd;
    }

    void registerCommands() {
        // Explicitly specify std::string return type for ALL command handlers
        command_handlers_["HELP"] = [](std::istringstream&,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                return "Available commands: HELP, ADD, START, PAUSE, RESUME, CANCEL, LIST, STATUS, THREADS";
            };

        command_handlers_["ADD"] = [this](std::istringstream& args,
            const std::function<void(int, size_t, size_t)>& progressReporter) -> std::string {
                std::string url, outputPath;
                args >> url >> outputPath;

                if (url.empty() || outputPath.empty()) {
                    return "ERROR Usage: ADD <url> <output_path>";
                }

                int taskId = downloader_->addTask(url, outputPath);

                // Set up progress callback for this task
                auto task = downloader_->getTask(taskId);
                if (task) {
                    task->setProgressCallback([taskId, progressReporter](size_t downloaded, size_t total) {
                        progressReporter(taskId, downloaded, total);
                        });
                }

                return "OK " + std::to_string(taskId);
            };

        command_handlers_["START"] = [this](std::istringstream& args,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                int taskId;
                args >> taskId;

                if (args.fail()) {
                    if (downloader_->startAll()) {
                        return "OK Started all tasks";
                    }
                    return "ERROR Failed to start all tasks";
                }

                if (downloader_->startTask(taskId)) {
                    return "OK Started task " + std::to_string(taskId);
                }
                return "ERROR Failed to start task " + std::to_string(taskId);
            };

        command_handlers_["PAUSE"] = [this](std::istringstream& args,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                int taskId;
                args >> taskId;

                if (args.fail()) {
                    if (downloader_->pauseAll()) {
                        return "OK Paused all tasks";
                    }
                    return "ERROR Failed to pause all tasks";
                }

                if (downloader_->pauseTask(taskId)) {
                    return "OK Paused task " + std::to_string(taskId);
                }
                return "ERROR Failed to pause task " + std::to_string(taskId);
            };

        command_handlers_["RESUME"] = [this](std::istringstream& args,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                int taskId;
                args >> taskId;

                if (args.fail()) {
                    if (downloader_->resumeAll()) {
                        return "OK Resumed all tasks";
                    }
                    return "ERROR Failed to resume all tasks";
                }

                if (downloader_->resumeTask(taskId)) {
                    return "OK Resumed task " + std::to_string(taskId);
                }
                return "ERROR Failed to resume task " + std::to_string(taskId);
            };

        command_handlers_["CANCEL"] = [this](std::istringstream& args,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                int taskId;
                args >> taskId;

                if (args.fail()) {
                    if (downloader_->cancelAll()) {
                        return "OK Cancelled all tasks";
                    }
                    return "ERROR Failed to cancel all tasks";
                }

                if (downloader_->cancelTask(taskId)) {
                    return "OK Cancelled task " + std::to_string(taskId);
                }
                return "ERROR Failed to cancel task " + std::to_string(taskId);
            };

        command_handlers_["LIST"] = [this](std::istringstream&,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                auto taskIds = downloader_->getTaskIds();
                if (taskIds.empty()) {
                    return "OK No tasks";
                }

                std::ostringstream response;
                response << "OK " << taskIds.size() << " tasks:";

                for (int id : taskIds) {
                    auto task = downloader_->getTask(id);
                    if (task) {
                        response << "\n" << id << ": " << task->getUrl() << " => " << task->getOutputPath()
                            << " [" << getStatusString(task->getStatus()) << "] "
                            << task->getDownloadedSize() << "/" << task->getTotalSize() << " bytes";
                    }
                }

                return response.str();
            };

        command_handlers_["STATUS"] = [this](std::istringstream& args,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                int taskId;
                args >> taskId;

                auto task = downloader_->getTask(taskId);
                if (!task) {
                    return "ERROR Task not found: " + std::to_string(taskId);
                }

                std::ostringstream response;
                response << "OK "
                    << "URL: " << task->getUrl() << "\n"
                    << "Output: " << task->getOutputPath() << "\n"
                    << "Status: " << getStatusString(task->getStatus()) << "\n"
                    << "Progress: " << task->getProgress() << "%\n"
                    << "Downloaded: " << task->getDownloadedSize() << " bytes\n"
                    << "Total size: " << task->getTotalSize() << " bytes";

                if (!task->getErrorMessage().empty()) {
                    response << "\nError: " << task->getErrorMessage();
                }

                return response.str();
            };

        command_handlers_["THREADS"] = [this](std::istringstream&,
            const std::function<void(int, size_t, size_t)>&) -> std::string {
                std::ostringstream response;
                response << "OK Thread pool status:\n"
                    << "- Pending tasks: " << threadPool_.get_pending_tasks_count() << "\n"
                    << "- Active threads: " << threadPool_.get_active_threads_count();
                return response.str();
            };
    }

    std::string getStatusString(DownloadTask::Status status) {
        switch (status) {
        case DownloadTask::Status::Idle: return "Idle";
        case DownloadTask::Status::Downloading: return "Downloading";
        case DownloadTask::Status::Paused: return "Paused";
        case DownloadTask::Status::Completed: return "Completed";
        case DownloadTask::Status::Failed: return "Failed";
        case DownloadTask::Status::Cancelled: return "Cancelled";
        default: return "Unknown";
        }
    }

private:
    int port_;
    int server_fd_ = -1;
    std::atomic<bool> running_;
    std::thread server_thread_;
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<Downloader> downloader_;
    ThreadPool threadPool_;
    std::unordered_map<std::string, std::function<std::string(std::istringstream&, const std::function<void(int, size_t, size_t)>&)>> command_handlers_;
};

int main(int argc, char* argv[]) {
    // Set default values
    int port = 10280;
    size_t threadPoolSize = 8; // Default thread pool size

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            threadPoolSize = std::stoi(argv[++i]);
        }
    }

    // Create and start server
    DownloadServer server(port, threadPoolSize);

    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }

    std::cout << "Server started on port " << port << " with thread pool size " << threadPoolSize << std::endl;
    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();

    server.stop();
    return 0;
}