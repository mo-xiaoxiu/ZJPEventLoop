#include "../include/EventLoop.h"
#include <iostream>
#include <chrono>
#include <string>
#include <atomic>

// Global counter for demonstration
std::atomic<int> counter{0};

// Callback function for one-shot timer
void oneShotCallback(void* arg) {
    std::string* message = static_cast<std::string*>(arg);
    std::cout << "[One-shot Timer] " << *message << std::endl;
}

// Callback function for repeating timer
void repeatingCallback(void* arg) {
    int* count = static_cast<int*>(arg);
    (*count)++;
    std::cout << "[Repeating Timer] Tick #" << *count << " at " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch()).count() 
              << "ms" << std::endl;
}

// Callback to stop the event loop after some time
void stopCallback(void* arg) {
    EventLoop* loop = static_cast<EventLoop*>(arg);
    std::cout << "[Stop Timer] Stopping event loop..." << std::endl;
    loop->stop();
}

int main() {
    std::cout << "=== EventLoop Timer Example ===" << std::endl;
    
    EventLoop& loop = EventLoop::getInstance();
    
    // Example 1: One-shot timers with different delays
    std::string msg1 = "Hello after 1 second!";
    std::string msg2 = "Hello after 2 seconds!";
    std::string msg3 = "Hello after 3 seconds!";
    
    uint64_t timer1 = loop.addTimer(1000, 0, oneShotCallback, &msg1);
    uint64_t timer2 = loop.addTimer(2000, 0, oneShotCallback, &msg2);
    uint64_t timer3 = loop.addTimer(3000, 0, oneShotCallback, &msg3);
    
    std::cout << "Added one-shot timers with IDs: " << timer1 << ", " << timer2 << ", " << timer3 << std::endl;
    
    // Example 2: Repeating timer
    int tickCount = 0;
    uint64_t repeatingTimer = loop.addTimer(0, 500, repeatingCallback, &tickCount);
    std::cout << "Added repeating timer with ID: " << repeatingTimer << std::endl;
    
    // Example 3: Timer modification
    std::string modifiedMsg = "Modified timer fired!";
    uint64_t modifiableTimer = loop.addTimer(5000, 0, oneShotCallback, &modifiedMsg);
    std::cout << "Added modifiable timer with ID: " << modifiableTimer << std::endl;
    
    // Modify the timer to fire earlier
    loop.addTask([&loop, modifiableTimer](void*) {
        std::cout << "[Task] Modifying timer " << modifiableTimer << " to fire in 1.5 seconds" << std::endl;
        loop.modifyTimer(modifiableTimer, 1500, 0);
    }, nullptr);
    
    // Example 4: Timer removal
    std::string cancelledMsg = "This should not appear!";
    uint64_t cancelledTimer = loop.addTimer(4000, 0, oneShotCallback, &cancelledMsg);
    std::cout << "Added timer to be cancelled with ID: " << cancelledTimer << std::endl;
    
    // Schedule timer removal
    loop.addTimer(1200, 0, [](void* arg) {
        auto* data = static_cast<std::pair<EventLoop*, uint64_t>*>(arg);
        std::cout << "[Cancel Timer] Removing timer " << data->second << std::endl;
        bool removed = data->first->removeTimer(data->second);
        std::cout << "[Cancel Timer] Timer removal " << (removed ? "successful" : "failed") << std::endl;
    }, new std::pair<EventLoop*, uint64_t>(&loop, cancelledTimer));
    
    // Example 5: Stop the event loop after 8 seconds
    loop.addTimer(8000, 0, stopCallback, &loop);
    
    // Show timer count
    std::cout << "Current timer count: " << loop.timerCount() << std::endl;
    
    // Start the event loop
    std::cout << "\nStarting event loop..." << std::endl;
    auto startTime = std::chrono::steady_clock::now();
    
    loop.run();
    
    // Wait for the loop to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(20000));
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "\nEvent loop finished after " << duration.count() << "ms" << std::endl;
    std::cout << "Final timer count: " << loop.timerCount() << std::endl;
    std::cout << "Repeating timer ticked " << tickCount << " times" << std::endl;
    
    return 0;
}
