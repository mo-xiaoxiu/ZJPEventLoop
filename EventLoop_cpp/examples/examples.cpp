#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>
#include <future>
#include <cstdlib>

#include "EventLoop.h"

static void do_work(unsigned iters) {
    // tiny CPU workload to amplify parallelism when thread pool is enabled
    volatile uint64_t acc = 0;
    for (unsigned i = 0; i < iters; ++i) acc += i * 2654435761u;
    (void)acc;
}

int main(int argc, char** argv) {
    // args: [num_tasks] [work_iters]
    size_t numTasks = (argc > 1) ? static_cast<size_t>(std::strtoull(argv[1], nullptr, 10)) : 200000;
    unsigned workIters = (argc > 2) ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : 0;

#ifdef USE_ZJP_THREADPOOL
    std::cout << "Thread pool: ON\n";
#else
    std::cout << "Thread pool: OFF\n";
#endif
    std::cout << "Tasks: " << numTasks << ", workIters/task: " << workIters << std::endl;
    
    EventLoop::getInstance().run();

    std::atomic<size_t> done{0};
    std::promise<void> allDonePromise;
    auto allDoneFuture = allDonePromise.get_future();

    auto t0 = std::chrono::steady_clock::now();

    for (size_t i = 0; i < numTasks; ++i) {
        EventLoop::getInstance().addTask([&done, &allDonePromise, numTasks, workIters](void*) {
            if (workIters) do_work(workIters);
            size_t cur = ++done;
            if (cur == numTasks) {
                allDonePromise.set_value();
            }
        }, nullptr);
    }

    // wait done
    allDoneFuture.wait();
    auto t1 = std::chrono::steady_clock::now();

    double secs = std::chrono::duration<double>(t1 - t0).count();
    double throughput = numTasks / (secs > 0 ? secs : 1e-9);
    std::cout << "Completed " << numTasks << " tasks in " << secs << " s, throughput: "
              << static_cast<long long>(throughput) << " tasks/s" << std::endl;

    // stop and exit
    EventLoop::getInstance().stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}