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

using EventCallback = std::function<void(void*)>;

using IoCallback = std::function<void(int /*fd*/, uint32_t /*events*/, void* /*arg*/)>;

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
};

#endif