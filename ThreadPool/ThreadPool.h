// ThreadPool.h
#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <functional>
#include <future>


class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    // Delete copy and move constructors and assignment operators
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Getters for thread pool status
    size_t get_pending_tasks_count() const;
    size_t get_active_threads_count() const;

    /*
     * Enqueue a task to be executed by the thread pool
     * @param f The function to be executed
     * @param args The arguments to be passed to the function
     * @return A future that will hold the result of the function
        1 使用右值引用是为了确保完美转发（保留传入值的类型），避免不必要的复制
        2 std::result_of 用于在编译时推导函数在给定参数时返回值的类型（依赖模版参数的嵌套类型不可省略 typename）
        3 返回 std::future 以便调用者可以等待任务完成并获取结果（是否完成、返回值、抛出异常）
     */
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

private:
    /*
      ******* 所有向量在构造时初始化，运行时不做修改（确保线程安全） ********
            1 每个线程都有自己的任务队列（一一对应），避免锁竞争
            2 线程空闲时会尝试从其他线程的任务队列中窃取任务，负载均衡
            3 每个任务队列都有自己的互斥锁，精细化的锁粒度
    */
    struct TaskQueue {
        mutable std::mutex mutex;
        std::queue<std::function<void()>> tasks; // 类型擦除（只保留调用类型）、延迟执行
    };
    std::vector<std::thread> workers;
    std::vector<TaskQueue> task_queues;
    std::vector<std::condition_variable> cvs;
    std::vector<std::mutex> mtxs;
    std::atomic<bool> stop{ false };

    void worker_loop(size_t index);
    bool steal_task(size_t victim_index, std::function<void()>& out_task);
};

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;

    /*
        ******* task_ptr 存储任务的可调用对象（函数、lambda、bind 表达式等） ********
        1 std::packaged_task 将可调用对象（函数、lambda、bind 表达式等）与 std::future 关联（实现异步任务的包装与管理）
        2 使用完美转发（std::forward）来保持参数的值类别（左值或右值）
        2 std::bind 用于创建函数对象，将函数调用参数预先绑定到函数对象中
        3 std::shared_ptr 用于共享所有权，确保任务在执行完成后不会被销毁
    */
    auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    // 任务亲和性调度索引 (affinity scheduling index)：当前线程倾向于使用自己的任务队列（通过哈希函数将线程提交的任务分散到队列上）
    size_t thread_index = std::hash<std::thread::id>{}(std::this_thread::get_id()) % task_queues.size();
    {
        std::lock_guard<std::mutex> lock(task_queues[thread_index].mutex); // 锁定当前线程的任务队列
        task_queues[thread_index].tasks.emplace([task_ptr]() { (*task_ptr)(); });
    }
    cvs[thread_index].notify_one(); // 通知一个线程有新任务可用
    return task_ptr->get_future(); // 返回 std::future 以便调用者可以等待任务完成并获取结果
}




