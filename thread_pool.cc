#include "experimental/users/aishwaryapundr/feature_flag_engine/thread_pool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) : stop_(false) {
    std::cout << "[ThreadPool] Creating pool with " << num_threads << " worker threads." << std::endl;
    for (size_t i = 0; i < num_threads; ++i) {
        // Capture 'i' by value so each thread knows its own index for logging
        workers_.emplace_back([this, i] {
            std::cout << "[ThreadPool] Worker thread " << i << " initialized." << std::endl;
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex_);

                    this->cv_.wait(lock, [this] {
                        return this->stop_ || !this->tasks_.empty();
                    });

                    if (this->stop_ && this->tasks_.empty()) {
                        std::cout << "[ThreadPool] Worker thread " << i << " exiting." << std::endl;
                        return;
                    }

                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                    std::cout << "[ThreadPool] Worker thread " << i << " picked up a task. Queue size: " 
                              << this->tasks_.size() << std::endl;
                }

                task();
                std::cout << "[ThreadPool] Worker thread " << i << " finished executing task." << std::endl;
            }
        });
    }
}

void ThreadPool::Enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        tasks_.push(std::move(task));
        std::cout << "[ThreadPool] Enqueued task. Queue size: " << tasks_.size() << std::endl;
    }
    cv_.notify_one();
}

ThreadPool::~ThreadPool() {
    std::cout << "[ThreadPool] Shutting down thread pool..." << std::endl;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_all();

    for (std::thread& worker : workers_) {
        worker.join();
    }
    std::cout << "[ThreadPool] Thread pool shut down complete." << std::endl;
}
