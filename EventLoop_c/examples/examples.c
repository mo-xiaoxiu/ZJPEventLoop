#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#include "../include/event_loop.h"

void test_callback_1(void *arg) {
    printf("%s called.\n", __FUNCTION__);
}

void test_callback_2(void *arg) {
    printf("%s called.\n", __FUNCTION__);
}

int main(int argc, char *argv[]) {
    event_loop_t loop;
    event_loop_init(&loop);
    event_loop_run(&loop);

    event_loop_add_task(&loop, test_callback_1, NULL);
    printf("event loop tasks size: %ld\n", event_loop_task_size(&loop));
    event_loop_add_task(&loop, test_callback_2, NULL);

    while (true) {
        sleep(1);
    }

    return 0;
}