#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads) 
        : shutdownRequested(false)
        , busyThreads(0) {
        try {
            //std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            for (size_t i = 0; i < numThreads; ++i) {
                threads.emplace_back(&ThreadPool::workerFunction, this);
            }
        } catch (...) {
            shutdown();
            throw;
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    template<typename F, typename... Args>
    auto addTask(F&& f, Args&&... args, std::optional<int> priority = std::nullopt)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using ReturnType = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> future = task->get_future();
        int taskPriority = priority.value_or(0);

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shutdownRequested) {
                //throw std::runtime_error("Cannot add tasks to a stopped ThreadPool");
            } else {
                tasks.emplace(taskPriority, [task]() { (*task)(); });
            }
        }

        conditionVariable.notify_one();
        return future;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdownRequested = true;
        }
        conditionVariable.notify_all();
        for (std::thread& worker : threads) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    size_t getNumBusyThreads() const {
        std::lock_guard<std::mutex> lock(mutex);
        return busyThreads.load();
    }

    size_t getNumThreads() const {
        std::lock_guard<std::mutex> lock(mutex);
        return threads.size();
    }

    long getQueueSize() const {
        std::lock_guard<std::mutex> lock(mutex);
        return tasks.size();
    }

    void purge() {
        std::lock_guard<std::mutex> lock(mutex);
        for (int i = 0; i < tasks.size(); i++) {
            tasks.pop();
        }
    }

private:
    struct TaskItem {
        int priority;
        std::function<void()> task;

        bool operator<(const TaskItem& other) const {
            return priority < other.priority;        
        }
    };

    void workerFunction() {
        double timeAvg = 10000;
        std::vector<TaskItem> localTaskQueue;
        int taskCount = 0;

        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                conditionVariable.wait(lock, [this]() {
                    return shutdownRequested || !tasks.empty();
                });

                if (shutdownRequested) {
                    return;
                }
                
                if (timeAvg > 0) taskCount = timebuffer / timeAvg;
                if (taskCount > maxTaskBufferSize) taskCount = maxTaskBufferSize;
                if (taskCount < 1) taskCount = 1;
                if (taskCount > tasks.size()) taskCount = tasks.size();

                localTaskQueue.resize(taskCount);

                for (auto& i : localTaskQueue) {
                    if (shutdownRequested && tasks.empty()) {
                        return;
                    }

                    i = std::move(tasks.top());
                    tasks.pop();
                }
            }
            busyThreads++;
            auto start = std::chrono::high_resolution_clock::now();
            for (auto& i : localTaskQueue) {
                if (i.task != nullptr) {
                    i.task();
                } else {
                    std::cout << "no function passed\n";
                }
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto time = end-start;
            timeAvg = (timeAvg*4+time.count()/(double)localTaskQueue.size())/5;
            //timeAvg = (double)time.count() / localTaskQueue.size();
            //std::cout << timeAvg << " | " << taskCount << "\n";
            busyThreads--;
        }
    }

    const long timebuffer = 1E7;
    double maxTaskBufferSize = 128;

    std::vector<std::thread> threads;
    std::priority_queue<TaskItem> tasks;

    mutable std::mutex mutex;
    std::condition_variable conditionVariable;
    bool shutdownRequested;
    std::atomic<size_t> busyThreads;
};