#ifndef _EVENT_LOOP_
#define _EVENT_LOOP_

#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <queue>

using EventCallback = std::function<void(void*)>;

using IoCallback = std::function<void(int /*fd*/, uint32_t /*events*/, void* /*arg*/)>;

using TimerCallback = std::function<void(void*)>;

enum class EvenLoopState{
    EV_READY,
    EV_BUSY,
};

/* task */
struct Task {
    struct Task *next;
    EventCallback evCallback;
    void *arg;

    Task(EventCallback cb, void *arg) : evCallback(cb), arg(arg) {}
};

/* Timer */
struct Timer {
    using TimerPoint = std::chrono::steady_clock::time_point;

    uint64_t id;
    TimerPoint expireTime;
    std::chrono::milliseconds interval;
    TimerCallback callback;
    void *arg;
    bool repeating;

    Timer() = default;
    Timer(uint64_t id, TimerPoint expire, std::chrono::milliseconds interval, 
        TimerCallback cb, void* arg, bool repeat = false)
      : id(id), expireTime(expire), interval(interval), callback(cb), arg(arg), repeating(repeat) {}

    bool operator<(const Timer &other) const {
        return expireTime > other.expireTime;
    }
};

/* event loop list */
class EventLoop {
private:
    std::queue<Task> taskQueue;
    size_t size{0};
    EvenLoopState state{EvenLoopState::EV_READY};
    std::thread eventThread;
    std::mutex eventLoopMtx;
    std::condition_variable eventLoopCond;

    // io multiplexer
    int epollFd{-1};
    int eventFd{-1};
    struct IoWatcher { IoCallback cb; void* arg; uint32_t events; };
    std::unordered_map<int, IoWatcher> watchers; // fd -> watcher

    // timer manager
    std::priority_queue<Timer> timerQueue;
    std::unordered_map<uint64_t, Timer> activeTimers; // id -> timer for quick lookup
    std::atomic<uint64_t> nextTimerId{1};   // next available timer ID
    std::mutex timerMutex;                  // protect timer data structures

    // lifecycle
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> running{false};

    EventLoop();

public:
    EventLoop(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    ~EventLoop();

    static EventLoop& getInstance();
    void run();
    void stop();
    void addTask(EventCallback cb, void *arg);
    size_t taskSize();

    // IO watcher management (events use epoll flags like EPOLLIN/EPOLLOUT)
    int addIo(int fd, uint32_t events, IoCallback cb, void* arg);
    int modIo(int fd, uint32_t events);
    int delIo(int fd);

    // Timer Manager
    uint64_t addTimer(uint64_t delay, uint64_t interval, TimerCallback cb, void *arg);
    bool removeTimer(uint64_t timerId);
    bool modifyTimer(uint64_t timerId, uint64_t newDelay, uint64_t newInterval);
    size_t timerCount();

    // Timeout strategy configuration
    static constexpr std::chrono::milliseconds MAX_EPOLL_TIMEOUT{1000};  // 1s max for I/O responsiveness
    static constexpr std::chrono::milliseconds TASK_PENDING_TIMEOUT{10}; // 10ms when tasks pending
    static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT{100};     // 100ms default

    // Helper methods for timer management
    std::chrono::milliseconds getNextTimerTimeout();
    void processExpiredTimers();

#ifdef USE_ZJP_THREADPOOL
    void addTaskToThreadpool(EventCallback cb, void *arg);
#endif
};

#endif