#ifndef TBB_MANAGER_HPP_
#define TBB_MANAGER_HPP_

#include <gflags/gflags.h>
#include <tbb/tbb.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logger.hpp"

DECLARE_string(custom_tbb_parallel_control);

namespace utils {

struct ThreadContext {
  uint64_t thread_id;
  uint64_t task_id;
  std::string task_name;
};

struct TBBState {
  bool initialized = false;
  std::shared_ptr<tbb::task_arena> arena;
};

/**
 * @brief TBB任务管理器，支持按名称管理arena和线程上下文
 */
class TBBManager {
 public:
  static TBBManager& GetInstance();

  std::shared_ptr<tbb::task_arena> Init(const std::string& tbb_name);

  template <typename IntType, typename Func>
  void ParallelFor(const std::string& tbb_name, IntType start, IntType end,
                   const Func& task);

  template <typename T, typename Func>
  void ParallelFor(const std::string& tbb_name,
                   const tbb::blocked_range<T>& range, const Func& task);

  void Release();
  ~TBBManager();

  static std::map<std::string, int> InitTBBParallelCountDefines();
  static std::map<std::string, int>& GetTBBParallelCountDefines();

 private:
  TBBManager() = default;
  TBBManager(const TBBManager&) = delete;
  TBBManager& operator=(const TBBManager&) = delete;

  uint64_t GenerateUniqueTaskId() const;

  std::shared_ptr<tbb::task_arena> GetArena(const std::string& tbb_name);

  void RecordContexts(const std::string& unique_task_name,
                      std::vector<ThreadContext>&& contexts);

  std::unordered_map<std::string, TBBState> task_arenas_;
  std::unordered_map<std::string, std::queue<ThreadContext>> thread_contexts_;

  mutable std::mutex arenas_mutex_;
  mutable std::mutex contexts_mutex_;
};

// 模板实现
template <typename IntType, typename Func>
void TBBManager::ParallelFor(const std::string& tbb_name, IntType start,
                             IntType end, const Func& task) {
  uint64_t task_id = GenerateUniqueTaskId();
  std::string unique_task_name = tbb_name + "_" + std::to_string(task_id);

  auto arena = Init(tbb_name);

  LOG(INFO) << "[TBBManager] ParallelFor start: " << unique_task_name << " ["
            << start << "," << end << ")";
  arena->execute(
      [this, task, tbb_name, task_id, unique_task_name, start, end]() {
        tbb::parallel_for(
            tbb::blocked_range<IntType>(start, end),
            [this, task, tbb_name, task_id,
             unique_task_name](const tbb::blocked_range<IntType>& range) {
              std::vector<ThreadContext> local_contexts;
              local_contexts.reserve(range.size());
              for (IntType i = range.begin(); i < range.end(); ++i) {
                ThreadContext context{
                    static_cast<uint64_t>(i),  // thread_id
                    task_id,                   // task_id
                    tbb_name                   // task_name
                };
                try {
                  task(i);
                } catch (const std::exception& e) {
                  LOG(ERROR) << "[TBBManager] Exception in task: " << e.what();
                }
                local_contexts.push_back(context);
              }
              if (!local_contexts.empty()) {
                RecordContexts(unique_task_name, std::move(local_contexts));
              }
            });
      });
  LOG(INFO) << "[TBBManager] ParallelFor end: " << unique_task_name;
}

template <typename T, typename Func>
void TBBManager::ParallelFor(const std::string& tbb_name,
                             const tbb::blocked_range<T>& range,
                             const Func& task) {
  uint64_t task_id = GenerateUniqueTaskId();
  std::string unique_task_name = tbb_name + "_" + std::to_string(task_id);

  auto arena = Init(tbb_name);

  LOG(INFO) << "[TBBManager] ParallelFor start: " << unique_task_name;
  arena->execute([this, &task, &range, tbb_name, task_id, unique_task_name]() {
    tbb::parallel_for(range, [this, &task, tbb_name, task_id, unique_task_name](
                                 const tbb::blocked_range<T>& sub_range) {
      std::vector<ThreadContext> local_contexts;
      local_contexts.reserve(sub_range.size());
      for (auto it = sub_range.begin(); it != sub_range.end(); ++it) {
        ThreadContext context{0, tbb_name, task_id};
        try {
          task(it);
        } catch (const std::exception& e) {
          LOG(ERROR) << "[TBBManager] Exception in task: " << e.what();
        }
        local_contexts.push_back(context);
      }
      if (!local_contexts.empty()) {
        RecordContexts(unique_task_name, std::move(local_contexts));
      }
    });
  });
  LOG(INFO) << "[TBBManager] ParallelFor end: " << unique_task_name;
}

#define ARENA_TBB_WITH_GFLAGS_PARALLEL_FOR(gflagsName, ...)                 \
  do {                                                                      \
    utils::TBBManager::GetInstance().ParallelFor(#gflagsName, __VA_ARGS__); \
  } while (0)

}  // namespace utils

#endif  // TBB_MANAGER_HPP_