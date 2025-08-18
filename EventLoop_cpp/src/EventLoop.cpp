#include "EventLoop.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <queue>
#include <vector>
#include <mutex>
#include <chrono>
#ifdef USE_ZJP_THREADPOOL
#include <threadloop/ThreadLoop.h>
#endif

EventLoop::EventLoop() {
    // setup epoll and eventfd
    epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) {
        perror("epoll_create1");
        std::abort();
    }
    eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eventFd < 0) {
        perror("eventfd");
        std::abort();
    }
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.ptr = reinterpret_cast<void*>(static_cast<intptr_t>(eventFd));
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, eventFd, &ev) < 0) {
        perror("epoll_ctl ADD eventFd");
        std::abort();
    }
}

EventLoop& EventLoop::getInstance() {
    static EventLoop instance;
    return instance;
}

EventLoop::~EventLoop() {
    stop();
    {
        std::lock_guard<std::mutex> lck(eventLoopMtx);
        state = EvenLoopState::EV_READY;
        eventLoopCond.notify_all();
        while (!taskQueue.empty()) {
            taskQueue.pop();
        }
    }
    if (eventThread.joinable()) {
        eventThread.join();
    }
#ifdef USE_ZJP_THREADPOOL
    if (zjpThreadloop::ThreadLoop::getThreadLoopInstance().isRunning()) {
        zjpThreadloop::ThreadLoop::getThreadLoopInstance().join();
    }
#endif
    if (epollFd >= 0) close(epollFd);
    if (eventFd >= 0) close(eventFd);
}

#ifdef USE_ZJP_THREADPOOL
void EventLoop::addTaskToThreadpool(EventCallback cb, void *arg) {
    zjpThreadloop::ThreadLoop::getThreadLoopInstance().addTask([cb, arg]() { cb(arg); });
}
#endif

void EventLoop::run() {
    if (running.load()) return;
    stopRequested.store(false);
    running.store(true);
    eventThread = std::thread([this]() {
        constexpr int MAX_EVENTS = 64;
        epoll_event events[MAX_EVENTS];
#ifdef USE_ZJP_THREADPOOL
        auto &pool = zjpThreadloop::ThreadLoop::getThreadLoopInstance();
        if (!pool.isRunning()) pool.start();
#endif
        while (!stopRequested.load(std::memory_order_relaxed)) {
            auto timeout = getNextTimerTimeout();
            int timeoutMs = static_cast<int>(timeout.count());
            int n = epoll_wait(epollFd, events, MAX_EVENTS, timeoutMs);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait");
                continue;
            }
            processExpiredTimers();

            for (int i = 0; i < n; ++i) {
                int fd = static_cast<int>(reinterpret_cast<intptr_t>(events[i].data.ptr));
                if (fd == eventFd) {
                    // drain eventfd
                    uint64_t cnt;
                    while (read(eventFd, &cnt, sizeof(cnt)) == sizeof(cnt)) {}
                    // process all queued tasks
                    while (true) {
                        Task task{nullptr, nullptr};
                        {
                            std::lock_guard<std::mutex> lock(eventLoopMtx);
                            if (taskQueue.empty()) break;
                            task = taskQueue.front();
                            taskQueue.pop();
                            size--;
                            if (size == 0) state = EvenLoopState::EV_READY;
                        }
                        if (task.evCallback) {
#ifdef USE_ZJP_THREADPOOL
                            auto cb = task.evCallback;
                            void* a = task.arg;
                            addTaskToThreadpool(cb, a);
#else
                            task.evCallback(task.arg);
#endif
                        }
                    }
                } else {
                    // I/O event
                    uint32_t evs = events[i].events;
                    IoWatcher watcher;
                    bool found = false;
                    {
                        std::lock_guard<std::mutex> lock(eventLoopMtx);
                        if (auto it = watchers.find(fd); it != watchers.end()) {
                            watcher = it->second; found = true;
                        }
                    }
                    if (found && watcher.cb) {
                        watcher.cb(fd, evs, watcher.arg);
                    }
                }
            }
        }
        running.store(false);
    });
}

void EventLoop::stop() {
    if (!running.load()) return;
    stopRequested.store(true);
    // wake epoll thread via eventfd
    uint64_t one = 1;
    ssize_t wr = write(eventFd, &one, sizeof(one));
    (void)wr;
}

void EventLoop::addTask(EventCallback cb, void *arg) {
    std::lock_guard<std::mutex> lck(eventLoopMtx);
    taskQueue.push(Task(cb, arg));
    state = EvenLoopState::EV_BUSY;
    size++;
    // wake via eventfd
    uint64_t one = 1;
    ssize_t wr = write(eventFd, &one, sizeof(one));
    (void)wr;
}

size_t EventLoop::taskSize() {
    std::lock_guard<std::mutex> lck(eventLoopMtx);
    return size;
}

int EventLoop::addIo(int fd, uint32_t events, IoCallback cb, void* arg) {
    if (fd < 0 || !cb) return -1;
    {
        std::lock_guard<std::mutex> lock(eventLoopMtx);
        watchers[fd] = IoWatcher{cb, arg, events};
    }
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl ADD fd");
        return -1;
    }
    return 0;
}

int EventLoop::modIo(int fd, uint32_t events) {
    {
        std::lock_guard<std::mutex> lock(eventLoopMtx);
        auto it = watchers.find(fd);
        if (it == watchers.end()) return -1;
        it->second.events = events;
    }
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        perror("epoll_ctl MOD fd");
        return -1;
    }
    return 0;
}

int EventLoop::delIo(int fd) {
    {
        std::lock_guard<std::mutex> lock(eventLoopMtx);
        watchers.erase(fd);
    }
    if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        perror("epoll_ctl DEL fd");
        return -1;
    }
    return 0;
}

// Timer management implementations
uint64_t EventLoop::addTimer(uint64_t delay, uint64_t interval, TimerCallback cb, void *arg) {
    if (!cb) return 0;
    
    bool repeatFlag = (interval == 0) ? false : true;

    auto now = std::chrono::steady_clock::now();
    auto expireTime = now + std::chrono::milliseconds(delay);
    uint64_t timerId = nextTimerId.fetch_add(1);

    Timer timer(timerId, expireTime, std::chrono::milliseconds(interval), cb, arg, repeatFlag);
    {
        std::lock_guard<std::mutex> lock(timerMutex);
        timerQueue.push(timer);
        activeTimers[timerId] = timer;
    }

    // Wake up the event loop to recalculate timeout
    uint64_t one = 1;
    ssize_t wr = write(eventFd, &one, sizeof(one));
    (void)wr;
    
    return timerId;
}

bool EventLoop::removeTimer(uint64_t timerId) {
    std::lock_guard<std::mutex> lock(timerMutex);
    auto it = activeTimers.find(timerId);
    if (it == activeTimers.end()) {
        return false;
    }
    activeTimers.erase(it);
    return true;
}

bool EventLoop::modifyTimer(uint64_t timerId, uint64_t newDelay, uint64_t newInterval) {    
    std::lock_guard<std::mutex> lock(timerMutex);
    auto it = activeTimers.find(timerId);
    if (it == activeTimers.end()) {
        return false;
    }
    bool newRepeatFlag = (newInterval == 0) ? false : true;
    
    Timer& oldTimer = it->second;
    auto now = std::chrono::steady_clock::now();
    
    Timer newTimer(timerId, now + std::chrono::milliseconds(newDelay), std::chrono::milliseconds(newInterval), 
                   oldTimer.callback, oldTimer.arg, newRepeatFlag);
    
    timerQueue.push(newTimer);
    activeTimers[timerId] = newTimer;
    
    return true;
}

size_t EventLoop::timerCount() {
    std::lock_guard<std::mutex> lock(timerMutex);
    return activeTimers.size();
}

std::chrono::milliseconds EventLoop::getNextTimerTimeout() {
    std::lock_guard<std::mutex> lock(timerMutex);
    
    auto now = std::chrono::steady_clock::now();
    
    // Clean up expired timers from the top of the queue
    while (!timerQueue.empty()) {
        Timer topTimer = timerQueue.top();
        
        // Check if this timer is still active
        auto it = activeTimers.find(topTimer.id);
        if (it == activeTimers.end()) {
            // Timer was removed, skip it
            continue;
        }
        
        // Check if timer has expired
        if (topTimer.expireTime <= now) {
            return std::chrono::milliseconds(0); // Immediate timeout
        }
        
        // Calculate timeout for next timer
        auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            topTimer.expireTime - now);
        
        // Apply intelligent timeout strategy:
        // 1. For very short timers (< 50ms), use exact timeout
        // 2. For medium timers (50ms - 1s), limit to reasonable max
        // 3. For long timers (> 1s), use fixed interval to maintain responsiveness
        
        if (timeout <= std::chrono::milliseconds(50)) {
            return timeout; // High precision for short timers
        } else if (timeout <= std::chrono::milliseconds(MAX_EPOLL_TIMEOUT)) {
            return std::min(timeout, std::chrono::milliseconds(DEFAULT_TIMEOUT)); // Cap at 100ms
        } else {
            return std::chrono::milliseconds(DEFAULT_TIMEOUT); // Fixed 100ms for long timers
        }
    }
    
    // No active timers, return moderate timeout for I/O responsiveness
    return std::chrono::milliseconds(DEFAULT_TIMEOUT);
}

void EventLoop::processExpiredTimers() {
    auto now = std::chrono::steady_clock::now();
    std::vector<Timer> expiredTimers;
    
    {
        std::lock_guard<std::mutex> lock(timerMutex);
        
        // Collect all expired timers
        while (!timerQueue.empty()) {
            Timer topTimer = timerQueue.top();
            
            // Check if this timer is still active
            auto it = activeTimers.find(topTimer.id);
            if (it == activeTimers.end()) {
                // Timer was removed, skip it
                timerQueue.pop();
                continue;
            }
            
            // Check if timer has expired
            if (topTimer.expireTime > now) {
                break; // No more expired timers
            }
            
            // Timer has expired
            expiredTimers.push_back(topTimer);
            timerQueue.pop();
            
            // If it's a repeating timer, reschedule it
            if (topTimer.repeating) {
                Timer newTimer(topTimer.id, now + topTimer.interval, 
                              topTimer.interval, topTimer.callback, 
                              topTimer.arg, true);
                timerQueue.push(newTimer);
                activeTimers[topTimer.id] = newTimer;
            } else {
                // Remove one-shot timer from active timers
                activeTimers.erase(topTimer.id);
            }
        }
    }
    
    // Execute expired timer callbacks outside of the lock
    for (const Timer& timer : expiredTimers) {
        if (timer.callback) {
#ifdef USE_ZJP_THREADPOOL
            addTaskToThreadpool([timer](void*) { timer.callback(timer.arg); }, nullptr);
#else
            timer.callback(timer.arg);
#endif
        }
    }
}
