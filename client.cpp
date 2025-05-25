#include <arpa/inet.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

class DownloadClient {
public:
  DownloadClient(const std::string &serverAddress, int port)
      : serverAddress_(serverAddress), port_(port), socket_fd_(-1),
        connected_(false), running_(false) {
    // Ignore SIGPIPE to prevent crashes when writing to closed sockets
    signal(SIGPIPE, SIG_IGN);
  }

  ~DownloadClient() { disconnect(); }

  bool connect() {
    if (connected_) {
      std::cerr << "Already connected to server" << std::endl;
      return true;
    }

    // Create socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
      std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
      return false;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);

    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, serverAddress_.c_str(), &server_addr.sin_addr) <=
        0) {
      std::cerr << "Invalid address: " << serverAddress_ << std::endl;
      close(socket_fd_);
      socket_fd_ = -1;
      return false;
    }

    // Connect to server
    if (::connect(socket_fd_, (struct sockaddr *)&server_addr,
                  sizeof(server_addr)) < 0) {
      std::cerr << "Connection failed: " << strerror(errno) << std::endl;
      close(socket_fd_);
      socket_fd_ = -1;
      return false;
    }

    connected_ = true;
    running_ = true;

    // Start response handling thread
    responseThread_ = std::thread(&DownloadClient::responseHandler, this);

    std::cout << "Connected to " << serverAddress_ << ":" << port_ << std::endl;
    return true;
  }

  void disconnect() {
    if (!connected_) {
      return;
    }

    running_ = false;

    // Close socket
    if (socket_fd_ >= 0) {
      close(socket_fd_);
      socket_fd_ = -1;
    }

    // Wait for response thread to finish
    if (responseThread_.joinable()) {
      responseThread_.join();
    }

    connected_ = false;
    std::cout << "Disconnected from server" << std::endl;
  }

  bool sendCommand(const std::string &command) {
    if (!connected_) {
      std::cerr << "Not connected to server" << std::endl;
      return false;
    }

    std::string cmd = command + "\r\n";
    ssize_t bytes_sent = send(socket_fd_, cmd.c_str(), cmd.length(), 0);

    if (bytes_sent < 0) {
      std::cerr << "Error sending command: " << strerror(errno) << std::endl;
      return false;
    }

    return true;
  }

  // Run interactive command loop
  void runCommandLoop() {
    std::cout << "Enter commands (type 'exit' to quit, 'help' for available "
                 "commands):"
              << std::endl;

    std::string command;
    while (connected_ && std::cout << "> " && std::getline(std::cin, command)) {
      if (command == "exit" || command == "quit") {
        break;
      }

      if (command == "clear") {
        // Clear progress bars and screen
        std::lock_guard<std::mutex> lock(progressMutex_);
        progressBars_.clear();
#ifdef _WIN32
        [[maybe_unused]] int ignored = system("cls");
#else
        [[maybe_unused]] int ignored = system("clear");
#endif
        continue;
      }

      if (!sendCommand(command)) {
        std::cerr << "Failed to send command" << std::endl;
        break;
      }
    }

    disconnect();
  }

private:
  void responseHandler() {
    char buffer[4096];

    while (running_) {
      // Clear buffer
      memset(buffer, 0, sizeof(buffer));

      // Receive response from server
      ssize_t bytes_read = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);

      if (bytes_read <= 0) {
        if (running_) {
          std::cerr << "\nConnection closed by server" << std::endl;
          connected_ = false;
        }
        break;
      }

      // Process response
      std::string response(buffer, bytes_read);
      processResponse(response);
    }
  }

  void processResponse(const std::string &response) {
    std::istringstream iss(response);
    std::string line;

    while (std::getline(iss, line)) {
      // Remove carriage return if present
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      // Skip empty lines
      if (line.empty()) {
        continue;
      }

      // Check if it's a progress update
      if (line.compare(0, 9, "PROGRESS ") == 0) {
        updateProgress(line.substr(9));
      } else {
        // Regular response, print it
        std::cout << "\r" << line << std::endl;
      }
    }
  }

  void updateProgress(const std::string &progressData) {
    // Parse progress data (format: "taskId:percentage%")
    size_t colonPos = progressData.find(':');
    if (colonPos == std::string::npos) {
      return;
    }

    int taskId = std::stoi(progressData.substr(0, colonPos));
    int percentage = std::stoi(progressData.substr(
        colonPos + 1, progressData.length() - colonPos - 2));

    // Update progress bar
    {
      std::lock_guard<std::mutex> lock(progressMutex_);
      progressBars_[taskId] = percentage;
    }

    // Display all progress bars
    displayProgressBars();
  }

  void displayProgressBars() {
    // Clear current line and move to beginning
    std::cout << "\r\033[K";

    // If we're in the middle of typing a command, don't overwrite it
    if (progressBars_.empty()) {
      std::cout << "> " << std::flush;
      return;
    }

    std::lock_guard<std::mutex> lock(progressMutex_);

    // Display progress bars
    for (const auto &[taskId, percentage] : progressBars_) {
      const int barWidth = 20;
      std::cout << "Task " << taskId << " [";

      int pos = barWidth * percentage / 100;
      for (int i = 0; i < barWidth; ++i) {
        if (i < pos)
          std::cout << "=";
        else if (i == pos)
          std::cout << ">";
        else
          std::cout << " ";
      }

      std::cout << "] " << percentage << "% ";

      // For multiple progress bars, add line breaks
      if (progressBars_.size() > 1 && taskId != progressBars_.rbegin()->first) {
        std::cout << std::endl << "\r";
      }
    }

    std::cout << std::flush;
  }

private:
  std::string serverAddress_;
  int port_;
  int socket_fd_;
  std::atomic<bool> connected_;
  std::atomic<bool> running_;
  std::thread responseThread_;
  std::map<int, int> progressBars_; // taskId -> percentage
  std::mutex progressMutex_;
};

void printUsage(const char *programName) {
  std::cout << "Usage: " << programName << " [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --host <address>   Server address (default: 127.0.0.1)"
            << std::endl;
  std::cout << "  --port <port>      Server port (default: 10280)" << std::endl;
}

int main(int argc, char *argv[]) {
  std::string serverAddress = "127.0.0.1";
  int port = 10280;

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      serverAddress = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      port = std::stoi(argv[++i]);
    } else if (arg == "--help") {
      printUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown option: " << arg << std::endl;
      printUsage(argv[0]);
      return 1;
    }
  }

  // Create and connect client
  DownloadClient client(serverAddress, port);

  if (!client.connect()) {
    return 1;
  }

  // Print welcome message and available commands
  std::cout << "Download Client" << std::endl;
  std::cout << "Type 'help' for available commands" << std::endl;

  // Run command loop
  client.runCommandLoop();

  return 0;
}