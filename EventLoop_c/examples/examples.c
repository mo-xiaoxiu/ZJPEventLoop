#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#include "../include/event_loop.h"

void test_callback_1(void *arg) {
    printf("%s called.\n", __FUNCTION__);
}

void test_callback_2(void *arg) {
    printf("%s called.\n", __FUNCTION__);
}

static void stdin_cb(int fd, unsigned int events, void *arg) {
    event_loop_t *loop = (event_loop_t*)arg;
    if (!(events & EPOLLIN)) return;
    char buf[256];
    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            /* strip trailing newlines */
            char *p = buf;
            while (*p) p++;
            while (p > buf && (p[-1] == '\n' || p[-1] == '\r')) *--p = '\0';

            if (strcmp(buf, "task1") == 0) {
                event_loop_add_task(loop, test_callback_1, NULL);
                printf("queued task1\n");
            } else if (strcmp(buf, "task2") == 0) {
                event_loop_add_task(loop, test_callback_2, NULL);
                printf("queued task2\n");
            } else if (strcmp(buf, "help") == 0) {
                printf("type 'task1' or 'task2' then Enter to enqueue tasks.\n");
            } else if (buf[0] != '\0') {
                printf("stdin: %s\n", buf);
            }
        } else if (n == 0) {
            /* EOF on stdin */
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("read stdin");
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    event_loop_t loop;
    event_loop_init(&loop);
    event_loop_run(&loop);

    event_loop_add_task(&loop, test_callback_1, NULL);
    printf("event loop tasks size: %ld\n", event_loop_task_size(&loop));
    event_loop_add_task(&loop, test_callback_2, NULL);

    /* set stdin non-blocking and register to event loop */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    event_loop_add_io(&loop, STDIN_FILENO, EPOLLIN, stdin_cb, &loop);

    event_loop_add_timer(&loop, 1000, 1000, test_callback_1, NULL);
    event_loop_add_timer(&loop, 2000, 0, test_callback_2, NULL);

    while (true) {
        sleep(1);
    }

    return 0;
}