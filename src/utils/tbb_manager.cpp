#include "tbb_manager.hpp"

#include <atomic>
#include <sstream>

namespace utils {

namespace {
std::atomic<uint64_t> global_task_id{0};
}  // namespace

TBBManager& TBBManager::GetInstance() {
  static TBBManager instance;
  return instance;
}

std::shared_ptr<tbb::task_arena> TBBManager::Init(const std::string& tbb_name) {
  std::lock_guard<std::mutex> lock(arenas_mutex_);
  auto& state = task_arenas_[tbb_name];
  if (!state.initialized) {
    int concurrency = 0;
    auto& defines = GetTBBParallelCountDefines();
    auto it = defines.find(tbb_name);
    if (it != defines.end()) {
      concurrency = it->second;
    }
    if (concurrency <= 0) {
      concurrency = tbb::info::default_concurrency();
    }
    state.arena = std::make_shared<tbb::task_arena>(concurrency);
    state.initialized = true;
    LOG(INFO) << "[TBBManager] Arena '" << tbb_name
              << "' initialized with concurrency: " << concurrency;
  }
  return state.arena;
}

void TBBManager::Release() {
  std::lock_guard<std::mutex> lock(arenas_mutex_);
  for (auto& kv : task_arenas_) {
    if (kv.second.arena) {
      kv.second.arena->terminate();
      kv.second.arena.reset();
      kv.second.initialized = false;
      LOG(INFO) << "[TBBManager] Arena '" << kv.first << "' released.";
    }
  }
  task_arenas_.clear();
  {
    std::lock_guard<std::mutex> ctx_lock(contexts_mutex_);
    thread_contexts_.clear();
  }
}

TBBManager::~TBBManager() { Release(); }

std::map<std::string, int> TBBManager::InitTBBParallelCountDefines() {
  std::map<std::string, int> defines;
  // 解析gflags字符串，格式如 "arena1:4,arena2:8"
  std::string cfg = FLAGS_custom_tbb_parallel_control;
  std::istringstream ss(cfg);
  std::string item;
  while (std::getline(ss, item, ',')) {
    auto pos = item.find(':');
    if (pos != std::string::npos) {
      std::string name = item.substr(0, pos);
      int val = std::stoi(item.substr(pos + 1));
      defines[name] = val;
    }
  }
  return defines;
}

std::map<std::string, int>& TBBManager::GetTBBParallelCountDefines() {
  static std::map<std::string, int> defines = InitTBBParallelCountDefines();
  return defines;
}

uint64_t TBBManager::GenerateUniqueTaskId() const {
  return global_task_id.fetch_add(1, std::memory_order_relaxed);
}

std::shared_ptr<tbb::task_arena> TBBManager::GetArena(
    const std::string& tbb_name) {
  std::lock_guard<std::mutex> lock(arenas_mutex_);
  auto it = task_arenas_.find(tbb_name);
  if (it == task_arenas_.end() || !it->second.initialized) {
    throw std::runtime_error("[TBBManager] Arena '" + tbb_name +
                             "' not initialized.");
  }
  return it->second.arena;
}

void TBBManager::RecordContexts(const std::string& unique_task_name,
                                std::vector<ThreadContext>&& contexts) {
  std::lock_guard<std::mutex> lock(contexts_mutex_);
  auto& queue = thread_contexts_[unique_task_name];
  for (auto&& ctx : contexts) {
    queue.push(std::move(ctx));
  }
  LOG(DEBUG) << "[TBBManager] Recorded " << contexts.size()
             << " contexts for task: " << unique_task_name;
}

}  // namespace utils