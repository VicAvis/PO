#include <condition_variable>
#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <shared_mutex>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

#define NUM_THREADS 4
#define WORKERS_PER_QUEUE (NUM_THREADS / 2)
#define MIN_TIME 2
#define MAX_TIME 15

std::mutex cout_mutex;
template<typename... Args>
void print_sync(Args&&... args) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    (std::cout << ... << std::forward<Args>(args)) << std::endl;
}

struct Task {
    int id;
    int execTime;
    std::function<void()> func;

    Task(int id = 0, int duration = 0, std::function<void()> func = [] {})
        : id(id), execTime(duration), func(std::move(func)) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = default;
    Task& operator=(Task&&) = default;
};

class tasksQueue {
    int totalExec = 0;
    std::shared_mutex mutexQ;
    std::queue<Task> tasks;
public:
    tasksQueue() = default;
    ~tasksQueue() { clear(); }

    tasksQueue(const tasksQueue&) = delete;
    tasksQueue& operator=(const tasksQueue&) = delete;
    tasksQueue(tasksQueue&&) = delete;
    tasksQueue& operator=(tasksQueue&&) = delete;

    bool empty() {
        std::shared_lock lock(mutexQ);
        return tasks.empty();
    }

    int size() {
        std::shared_lock lock(mutexQ);
        return tasks.size();
    }

    void clear() {
        std::unique_lock lock(mutexQ);
        while (!tasks.empty()) {
            tasks.pop();
        }
        totalExec = 0;
    }

    bool pop(Task& task) {
        std::unique_lock lock(mutexQ);
        if (tasks.empty()) return false;
        task = std::move(tasks.front()); // Task& task now has struct that was required to be popped
        tasks.pop();
        totalExec -= task.execTime;
        return true;
    }

    bool push(int taskId, int execTime, std::function<void()> func) {
        Task newTask(taskId, execTime, std::move(func));
        std::unique_lock lock(mutexQ);
        tasks.push(std::move(newTask));
        totalExec += execTime;
        return true;
    }

    int getTotalExecTime() {
        std::shared_lock lock(mutexQ);
        return totalExec;
    }
};

class threadPool {
    std::atomic<int> totalExecutionTimeQ1{0};
    std::atomic<int> totalExecutionTimeQ2{0};

    std::atomic<int> totalWaitTimeQ1{0};
    std::atomic<int> totalWaitTimeQ2{0};
    std::atomic<int> processedTasksQ1{0};
    std::atomic<int> processedTasksQ2{0};
    std::chrono::steady_clock::time_point last_measurement;
    std::vector<int> q1AllSizes;
    std::vector<int> q2AllSizes;

    std::atomic<int> totalTasks{0};
    std::vector<std::thread> workers;
    tasksQueue queue1;
    tasksQueue queue2;

    std::mutex q1_mutex;
    std::condition_variable q1_cv;
    std::mutex q2_mutex;
    std::condition_variable q2_cv;

    std::mutex addTask;
    std::mutex poolStateMutex; // for stop/terminate/pause

    std::atomic<bool> terminated{false};
    std::atomic<bool> initialized{false};
    std::atomic<bool> paused{false};

    bool working_unsafe() const {
        return initialized.load(std::memory_order_relaxed) &&
               !terminated.load(std::memory_order_relaxed);
    }

public:
    bool working() {
        std::lock_guard<std::mutex> lock(poolStateMutex);
        return working_unsafe();
    }

    void initialize(int workerNumber) {
        std::lock_guard<std::mutex> lock(poolStateMutex);
        if (initialized || terminated) return;

        workers.reserve(NUM_THREADS);
        print_sync("[ThreadPool] Initializing with ", NUM_THREADS, " workers...");

        for (int i = 0; i < WORKERS_PER_QUEUE; ++i) {
            workers.emplace_back(&threadPool::routine, this, i, "Queue 1", std::ref(queue1), std::ref(q1_mutex), std::ref(q1_cv));
        }
        for (int i = WORKERS_PER_QUEUE; i < NUM_THREADS; ++i) {
            workers.emplace_back(&threadPool::routine, this, i, "Queue 2", std::ref(queue2), std::ref(q2_mutex), std::ref(q2_cv));
        }
        initialized = true;
        paused = false;
        terminated = false;
        print_sync("[ThreadPool] Initialized.");
    }

void add_task(int execTime, std::function<void()> func) {
        if (!initialized.load(std::memory_order_relaxed) || terminated.load(std::memory_order_relaxed)) {
            print_sync("[ThreadPool] Warning: Failed to add new task. Pool not ready or terminated.");
            return;
        }

        int taskId = totalTasks.fetch_add(1);
        tasksQueue* targetQueuePtr = nullptr;
        std::mutex* targetMutexPtr = nullptr;
        std::condition_variable* targetCVPtr = nullptr;
        std::string targetQueueName;
        int finalTime1 = 0;
        int finalTime2 = 0;

        {  // critical section for adding task
            std::lock_guard<std::mutex> lock(addTask);

            int time1 = queue1.getTotalExecTime();
            int time2 = queue2.getTotalExecTime();
            print_sync("[Debug Task ", taskId, ": Read times -> Q1=", time1, "s, Q2=", time2, "s");

            if (time1 <= time2) {
                targetQueuePtr = &queue1;
                targetMutexPtr = &q1_mutex;
                targetCVPtr = &q1_cv;
                targetQueueName = "Queue 1";
            } else {
                targetQueuePtr = &queue2;
                targetMutexPtr = &q2_mutex;
                targetCVPtr = &q2_cv;
                targetQueueName = "Queue 2";
            }
            print_sync("[Debug] Task ", taskId, ": Decided on ", targetQueueName);

            targetQueuePtr->push(taskId, execTime, std::move(func));

            measureQueueSizes();
            finalTime1 = queue1.getTotalExecTime();
            finalTime2 = queue2.getTotalExecTime();

        } // end of critical section
        print_sync("[ThreadPool] Task ", taskId, " (", execTime, "s) added to ", targetQueueName,
                    ". Final Q times -> Q1=", finalTime1, "s, Q2=", finalTime2, "s");

        {
            std::lock_guard<std::mutex> lock(*targetMutexPtr); // locking the correct's queue
            targetCVPtr->notify_one();
        }
    }

    void routine(int worker_id, const char* queue_name, tasksQueue& assignedQueue, std::mutex& assignedMutex, std::condition_variable& assignedCV) {
        print_sync("[Worker ", worker_id, "] started, assigned to ", queue_name);

        while (true) {
            Task task;
            bool task_popped = false;
            auto start = std::chrono::steady_clock::now();
            {
                std::unique_lock<std::mutex> lock(assignedMutex);
                assignedCV.wait(lock, [&] {
                    bool term = terminated.load(std::memory_order_relaxed);
                    bool pause = paused.load(std::memory_order_relaxed);

                    return term || (!pause && !assignedQueue.empty());
                });

                if (terminated.load(std::memory_order_relaxed)) {
                     print_sync("[Worker ", worker_id, "] Terminating signal detected.");
                    break;
                }

                if (paused.load(std::memory_order_relaxed)) {
                    print_sync("[Worker ", worker_id, "] Paused, continuing wait.");
                    continue; // go back to wait
                }

                 if (!assignedQueue.empty()) {
                    task_popped = assignedQueue.pop(task); // task is taken
                 }
                // lock released here
            }

            if (task_popped) {
                auto end = std::chrono::steady_clock::now();
                auto waitTime = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
                if (queue_name == "Queue 1") { totalWaitTimeQ1.fetch_add(waitTime, std::memory_order_relaxed);
                    processedTasksQ1.fetch_add(1, std::memory_order_relaxed);;
                } else { totalWaitTimeQ2.fetch_add(waitTime, std::memory_order_relaxed);
                    processedTasksQ2.fetch_add(1, std::memory_order_relaxed); }

                print_sync("[Worker ", worker_id, "] Executing Task ", task.id, " from ", queue_name,
                          " (", task.execTime, "s)...");
                auto execStart = std::chrono::steady_clock::now();
                task.func();
                auto execEnd = std::chrono::steady_clock::now();
                auto execDuration = std::chrono::duration_cast<std::chrono::seconds>(execEnd - execStart).count();
                if (queue_name == "Queue 1") {
                    totalExecutionTimeQ1 += execDuration;
                } else {
                    totalExecutionTimeQ2 += execDuration;
                }
                print_sync("[Worker ", worker_id, "] Finished Task ", task.id);
            }
        } // end while loop
         print_sync("[Worker ", worker_id, "] Exiting routine.");
    }

    void pause() {
        std::lock_guard<std::mutex> lock(poolStateMutex);
        if (!initialized.load(std::memory_order_relaxed) || terminated.load(std::memory_order_relaxed)) return;
        paused = true;
        print_sync("[ThreadPool] Paused.");
    }

    void unpause() {
        bool needs_notify = false;
        {
            std::lock_guard<std::mutex> lock(poolStateMutex);
            if (!initialized.load(std::memory_order_relaxed) ||
                terminated.load(std::memory_order_relaxed) ||
                !paused.load(std::memory_order_relaxed)) {
                 return;
            }
            paused = false;
            needs_notify = true;
            print_sync("[ThreadPool] Unpaused.");
        } // releasing poolStateMutex

        if (needs_notify) {  // notifying all workers on both queues
            { std::lock_guard lock1(q1_mutex); q1_cv.notify_all(); }
            { std::lock_guard lock2(q2_mutex); q2_cv.notify_all(); }
        }
    }

    void terminate() {
        bool needs_notify = false;
        {
            std::lock_guard<std::mutex> lock(poolStateMutex);
            if (!initialized.load() || terminated.load()) {
                return;
            }
            terminated = true;
            paused = false;  //  not stuck paused
            needs_notify = true;
            print_sync("[ThreadPool] Terminating...");
        } // release poolStateMutex

        if (needs_notify) {
            { std::lock_guard<std::mutex> lock1(q1_mutex); q1_cv.notify_all(); }
            { std::lock_guard<std::mutex> lock2(q2_mutex); q2_cv.notify_all(); }
             print_sync("[ThreadPool] Termination signals sent.");
        }

        print_sync("[ThreadPool] Joining worker threads...");
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
         print_sync("[ThreadPool] Worker threads joined.");

        std::lock_guard<std::mutex> lock(poolStateMutex);
        workers.clear();
        queue1.clear();
        queue2.clear();
        initialized = false;
        terminated = false;
        paused = false;
        totalTasks = 0;
        print_sync("[ThreadPool] Terminated and cleaned.");
    }

    void measureQueueSizes() {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_measurement).count() >= 100) {
            q1AllSizes.push_back(queue1.size());
            q2AllSizes.push_back(queue2.size());
            last_measurement = now;
        }
    }

    double safeAverage(std::atomic<int> const& total, std::atomic<int> const& count) const {
        int c = count.load(std::memory_order_acquire);
        return (c > 0) ? static_cast<double>(total.load(std::memory_order_acquire)) / c : 0.0;
    }

    double safeAverage(const std::vector<int>& samples) const {
        return samples.empty() ? 0.0 : std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    }

    double getAverageWaitTimeQ1() const { return safeAverage(totalWaitTimeQ1, processedTasksQ1); }
    double getAverageWaitTimeQ2() const { return safeAverage(totalWaitTimeQ2, processedTasksQ2); }
    double getAverageExecTimeQ1() const { return safeAverage(totalExecutionTimeQ1, processedTasksQ1); }
    double getAverageExecTimeQ2() const { return safeAverage(totalExecutionTimeQ2, processedTasksQ2); }
    double getAverageQ1Size() const { return safeAverage(q1AllSizes); }
    double getAverageQ2Size() const { return safeAverage(q2AllSizes); }

    void statistics() {
        print_sync("\n[ThreadPool] Statistics.");
        print_sync("Average wait time Q1: ", getAverageWaitTimeQ1());
        print_sync("Average wait time Q2: ", getAverageWaitTimeQ2());
        print_sync("Average execution time Q1: ", getAverageExecTimeQ1());
        print_sync("Average execution time Q2: ", getAverageExecTimeQ2());
        print_sync("Average Q1 size: ", getAverageQ1Size());
        print_sync("Average Q2 size: ", getAverageQ2Size());
    }

};

std::mt19937 generator(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));

int randomTime(int min, int max) {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

int main() {
    threadPool pool;
    print_sync("Main: Initializing ThreadPool.");
    pool.initialize(NUM_THREADS);

    auto task_generator = [&](int generator_id) {
        print_sync("[Producer ", generator_id, "] Started.");
        for (int i = 0; i < 5; ++i) {
            int taskDuration = randomTime(MIN_TIME, MAX_TIME);
            pool.add_task(taskDuration, [taskDuration, generator_id, i]() {
                std::this_thread::sleep_for(std::chrono::seconds(taskDuration));
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(100 + randomTime(0, 400)));
        }
         print_sync("[Producer ", generator_id, "] Finished.");
    };

    print_sync("Main: Starting producer threads.");
    std::vector<std::thread> producers;
    for (int i = 0; i < 3; ++i) {
        producers.emplace_back(task_generator, i + 1);
    }

    print_sync("Main: Waiting for producers to finish...");
    for (auto& producer : producers) {
        if(producer.joinable()) producer.join();
    }
     print_sync("Main: Producers finished.");

    std::this_thread::sleep_for(std::chrono::seconds(10));

    pool.pause();
    std::this_thread::sleep_for(std::chrono::seconds(20));
    pool.unpause();

    std::this_thread::sleep_for(std::chrono::seconds(20));
    pool.terminate();
    pool.statistics();

    print_sync("Main: Finished.");
    return 0;
}