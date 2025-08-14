#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "event_loop.h"

static struct _io_watcher* find_io_watcher(event_loop_t *loop, int fd);

static void* event_loop_worker(void *arg) {
    event_loop_t *loop = (event_loop_t *)arg;

    const int MAX_EVENTS = 64;
    struct epoll_event events[64];

    while (true) {
        int n = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = (int)(intptr_t)events[i].data.ptr; /* we store fd via pointer cast */

            if (fd == loop->event_fd) {
                /* drain eventfd */
                uint64_t cnt;
                while (read(loop->event_fd, &cnt, sizeof(cnt)) == sizeof(cnt)) { /* drain */ }

                /* process all queued tasks */
                while (true) {
                    pthread_mutex_lock(&loop->event_loop_mtx);
                    task_t *task = loop->task_head;
                    if (task) {
                        loop->task_head = task->next;
                        if (loop->task_head == NULL) {
                            loop->task_tail = NULL;
                        }
                        loop->size--;
                        if (loop->size == 0) loop->event_loop_state = EV_READY;
                    }
                    pthread_mutex_unlock(&loop->event_loop_mtx);

                    if (!task) break;
                    task->ev_cb(task->arg);
                    free(task);
                }
            } else {
                /* I/O event */
                unsigned int evs = events[i].events;
                struct _io_watcher *w = find_io_watcher(loop, fd);
                if (w && w->cb) {
                    w->cb(fd, evs, w->arg);
                }
            }
        }
    }

    return NULL;
}

void event_loop_init(event_loop_t *loop) {
    loop->task_head = loop->task_tail = NULL;
    loop->size = 0;
    loop->event_loop_state = EV_READY;
    pthread_mutex_init(&loop->event_loop_mtx, NULL);
    pthread_cond_init(&loop->event_loop_cond, NULL);

    /* epoll + eventfd setup */
    loop->io_head = NULL;
    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epoll_fd < 0) {
        perror("epoll_create1");
        exit(1);
    }
    loop->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (loop->event_fd < 0) {
        perror("eventfd");
        exit(1);
    }
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = (void*)(intptr_t)loop->event_fd; /* store fd */
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, loop->event_fd, &ev) < 0) {
        perror("epoll_ctl ADD event_fd");
        exit(1);
    }
}

void event_loop_run(event_loop_t *loop) {
    pthread_create(&loop->event_thread, NULL, event_loop_worker, (void *)loop);
}

void event_loop_add_task(event_loop_t *loop, event_callback_t cb, void *arg) {
    if (cb == NULL) return;

    task_t *node = (task_t*)malloc(sizeof(task_t));
    memset(node, 0, sizeof(task_t));
    node->ev_cb = cb;
    node->arg = arg;
    node->next = NULL;

    pthread_mutex_lock(&loop->event_loop_mtx);
    if (loop->task_tail == NULL) {
        loop->task_head = loop->task_tail = node;
    } else {
        loop->task_tail->next = node;
        loop->task_tail = node;
    }
    loop->size++;
    loop->event_loop_state = EV_BUSY;
    pthread_mutex_unlock(&loop->event_loop_mtx);
    /* wake via eventfd */
    uint64_t one = 1;
    ssize_t wr = write(loop->event_fd, &one, sizeof(one));
    (void)wr; /* ignore EAGAIN */
}

size_t event_loop_task_size(event_loop_t *loop) {
    pthread_mutex_lock(&loop->event_loop_mtx);
    size_t size = loop->size;
    pthread_mutex_unlock(&loop->event_loop_mtx);
    return size;
}

static struct _io_watcher* find_io_watcher(event_loop_t *loop, int fd) {
    struct _io_watcher *cur = loop->io_head;
    while (cur) {
        if (cur->fd == fd) return cur;
        cur = cur->next;
    }
    return NULL;
}

int event_loop_add_io(event_loop_t *loop, int fd, unsigned int events, io_callback_t cb, void *arg) {
    if (fd < 0 || cb == NULL) return -1;
    struct _io_watcher *w = (struct _io_watcher*)malloc(sizeof(*w));
    if (!w) return -1;
    w->fd = fd;
    w->events = events;
    w->cb = cb;
    w->arg = arg;
    pthread_mutex_lock(&loop->event_loop_mtx);
    w->next = loop->io_head;
    loop->io_head = w;
    pthread_mutex_unlock(&loop->event_loop_mtx);

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = (void*)(intptr_t)fd; /* store fd */
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl ADD fd");
        return -1;
    }
    return 0;
}

int event_loop_mod_io(event_loop_t *loop, int fd, unsigned int events) {
    struct _io_watcher *w = find_io_watcher(loop, fd);
    if (!w) return -1;
    w->events = events;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = (void*)(intptr_t)fd;
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        perror("epoll_ctl MOD fd");
        return -1;
    }
    return 0;
}

int event_loop_del_io(event_loop_t *loop, int fd) {
    /* remove from list */
    pthread_mutex_lock(&loop->event_loop_mtx);
    struct _io_watcher **pp = &loop->io_head;
    while (*pp) {
        if ((*pp)->fd == fd) {
            struct _io_watcher *rm = *pp;
            *pp = rm->next;
            free(rm);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&loop->event_loop_mtx);

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        perror("epoll_ctl DEL fd");
        return -1;
    }
    return 0;
}
