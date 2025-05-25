#include "ThreadPool.h"
#include <iostream>
#include <random>

ThreadPool::ThreadPool(size_t numThreads) : workers(numThreads), task_queues(numThreads), cvs(numThreads), mtxs(numThreads), stop(false) {    for (size_t i = 0; i < numThreads; ++i) {
        workers[i] = std::thread(&ThreadPool::worker_loop, this, i); // 创建线程（worker_loop）并传入索引
    }
}

ThreadPool::~ThreadPool() {
    // 通知所有线程停止工作
    stop.store(true);
    for (size_t i = 0; i < workers.size(); ++i) {
        cvs[i].notify_all();
    }
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::worker_loop(size_t index) {
    // 随机选择一个任务队列进行窃取
    std::random_device rd;  // 随机数生成器
    std::mt19937 gen(rd()); // 随机数引擎


    while (!stop.load()) {
        std::function<void()> task;
        bool found_task = false;

        // 首先尝试从自己的任务队列中获取任务
        {
            std::lock_guard<std::mutex> lock(task_queues[index].mutex);
            if (!task_queues[index].tasks.empty()) {
                task = std::move(task_queues[index].tasks.front());
                task_queues[index].tasks.pop();
                found_task = true;
            }
        }

        // 如果没有任务，则尝试从其他线程的任务队列中窃取任务
        if (!found_task) {
            std::uniform_int_distribution<size_t> dist(0, task_queues.size() - 1);
            size_t victim_index = dist(gen);
            if (victim_index != index) {
                found_task = steal_task(victim_index, task);
            }
        }

        // 执行任务或等待新任务
        if (found_task) {
            task();
        } else {
            std::unique_lock<std::mutex> lock(mtxs[index]);
            cvs[index].wait(lock, [this, index] {
                return stop.load() || !task_queues[index].tasks.empty();
                });
        }
    }
}

bool ThreadPool::steal_task(size_t victim_index, std::function<void()>& out_task) {
    std::lock_guard<std::mutex> lock(task_queues[victim_index].mutex);
    if (!task_queues[victim_index].tasks.empty()) {
        out_task = std::move(task_queues[victim_index].tasks.front());
        task_queues[victim_index].tasks.pop();
        return true;
    }
    return false;
}

// 待处理的任务数量
size_t ThreadPool::get_pending_tasks_count() const {
    size_t count = 0;
    for (const auto& queue : task_queues) {
        std::lock_guard<std::mutex> lock(queue.mutex);
        count += queue.tasks.size();
    }
    return count;
}

// 获得当前活动线程的数量（执行任务中）
size_t ThreadPool::get_active_threads_count() const {
    return std::count_if(workers.begin(), workers.end(), [](const std::thread& t) { return t.joinable(); });
}

