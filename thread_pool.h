#ifndef EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_THREAD_POOL_H_
#define EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_THREAD_POOL_H_

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
public:
    // Initialize the pool with a specific number of worker threads
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Add a new task to the queue. 
    // The task is a function that returns void and takes no arguments.
    void Enqueue(std::function<void()> task);

private:
    // The list of worker threads
    std::vector<std::thread> workers_;
    
    // The queue of tasks waiting to be processed
    std::queue<std::function<void()>> tasks_;

    // Synchronization primitives
    std::mutex queue_mutex_;          // Protects access to the task queue
    std::condition_variable cv_;     // Wakes up workers when tasks arrive
    bool stop_;                      // Set to true to shut down the pool
};

#endif  // EXPERIMENTAL_USERS_AISHWARYAPUNDR_FEATURE_FLAG_ENGINE_THREAD_POOL_H_
