#include <iostream>
#include <unistd.h>

#include "EventLoop.h"

int main () {
    EventLoop::getInstance().run();
    EventLoop::getInstance().addTask([](void *arg) {
        std::cout << "Hello, World!" << std::endl;
    }, NULL);

    while (true) {
        sleep(1);
    }
    return 0;
}