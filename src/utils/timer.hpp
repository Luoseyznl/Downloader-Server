#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace utils {
class Timer {
 public:
  struct TimerTask {
    std::chrono::steady_clock::time_point execTimestamp;
    std::function<void()> callback;
    bool isPeriodic;
    std::chrono::milliseconds period;

    TimerTask(
        std::chrono::steady_clock::time_point execTime,
        std::function<void()> cb, bool periodic = false,
        std::chrono::milliseconds periodDuration = std::chrono::milliseconds(0))
        : execTimestamp(execTime),
          callback(std::move(cb)),
          isPeriodic(periodic),
          period(periodDuration) {}
    bool operator>(const TimerTask& other) const {
      return execTimestamp > other.execTimestamp;
    }
  };

  explicit Timer();
  ~Timer();

  void addOnceTask(std::chrono::milliseconds delay,
                   std::function<void()> callback);
  void addPeriodicTask(std::chrono::milliseconds delay,
                       std::chrono::milliseconds period,
                       std::function<void()> callback);
  void start();
  void stop();

 private:
  std::priority_queue<TimerTask, std::vector<TimerTask>,
                      std::greater<TimerTask>>
      taskQueue_;
  std::mutex tasksMutex_;
  std::condition_variable tasksCv_;
  std::thread timerThread_;
  bool running_;
};
}  // namespace utils
