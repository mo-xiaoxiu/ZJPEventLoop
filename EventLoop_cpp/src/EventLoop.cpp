#include "EventLoop.h"

EventLoop::EventLoop() {}

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
}

void EventLoop::run() {
    eventThread = std::thread([this]() {
        while (true) {
            std::unique_lock<std::mutex> lock(eventLoopMtx);
            eventLoopCond.wait(lock, [this]() {
                return state == EvenLoopState::EV_BUSY;
            });
            if (state == EvenLoopState::EV_READY) {
                break;
            }
            // check taskQueue is empty again
            if (taskQueue.empty()) {
                continue;
            }
            Task task = taskQueue.front();
            taskQueue.pop();
            size--;
            lock.unlock();
            if (task.evCallback)
                task.evCallback(task.arg);
        }
    });
}

void EventLoop::addTask(EventCallback cb, void *arg) {
    std::lock_guard<std::mutex> lck(eventLoopMtx);
    taskQueue.push(Task(cb, arg));
    state = EvenLoopState::EV_BUSY;
    size++;
    eventLoopCond.notify_one();
}

size_t EventLoop::taskSize() {
    std::lock_guard<std::mutex> lck(eventLoopMtx);
    return size;
}
