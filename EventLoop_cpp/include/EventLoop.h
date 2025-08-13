#ifndef _EVENT_LOOP_
#define _EVENT_LOOP_

#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>

using EventCallback = std::function<void(void*)>;

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

    EventLoop();

public:
    EventLoop(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    ~EventLoop();

    static EventLoop& getInstance();
    void run();
    void addTask(EventCallback cb, void *arg);
    size_t taskSize();
};

#endif