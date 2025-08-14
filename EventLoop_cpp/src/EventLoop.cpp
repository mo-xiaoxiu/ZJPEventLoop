#include "EventLoop.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>

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
    if (epollFd >= 0) close(epollFd);
    if (eventFd >= 0) close(eventFd);
}

void EventLoop::run() {
    eventThread = std::thread([this]() {
        constexpr int MAX_EVENTS = 64;
        epoll_event events[MAX_EVENTS];
        while (true) {
            int n = epoll_wait(epollFd, events, MAX_EVENTS, -1);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait");
                continue;
            }
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
                        if (task.evCallback) task.evCallback(task.arg);
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
    });
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
