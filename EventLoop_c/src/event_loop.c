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
#include <time.h>
#include <sys/time.h>

#include "event_loop.h"

static struct _io_watcher* find_io_watcher(event_loop_t *loop, int fd);

// Get current time in milliseconds
static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Calculate timeout for epoll_wait based on next timer
static int get_next_timer_timeout(event_loop_t *loop) {
    pthread_mutex_lock(&loop->timer_mtx);
    
    if (!loop->timer_head) {
        pthread_mutex_unlock(&loop->timer_mtx);
        return -1; // No timers, block indefinitely
    }
    
    uint64_t now = get_current_time_ms();
    uint64_t next_expire = loop->timer_head->expire_time;
    
    pthread_mutex_unlock(&loop->timer_mtx);
    
    if (next_expire <= now) {
        return 0; // Timer already expired, don't block
    }
    
    int timeout = (int)(next_expire - now);
    return timeout > 100 ? 100 : timeout; // Cap at 100ms for responsiveness
}

// Insert timer into sorted list (earliest first)
static void insert_timer_sorted(event_loop_t *loop, timer_zjp_t *timer) {
    timer_zjp_t **current = &loop->timer_head;
    
    while (*current && (*current)->expire_time <= timer->expire_time) {
        current = &(*current)->next;
    }
    
    timer->next = *current;
    *current = timer;
}

// Process all expired timers
static void process_expired_timers(event_loop_t *loop) {
    uint64_t now = get_current_time_ms();
    timer_zjp_t *expired_list = NULL;
    
    pthread_mutex_lock(&loop->timer_mtx);
    
    // Collect expired timers
    while (loop->timer_head && loop->timer_head->expire_time <= now) {
        timer_zjp_t *expired = loop->timer_head;
        loop->timer_head = expired->next;
        
        // Add to expired list
        expired->next = expired_list;
        expired_list = expired;
    }
    
    pthread_mutex_unlock(&loop->timer_mtx);
    
    // Execute callbacks and handle repeating timers
    while (expired_list) {
        timer_zjp_t *timer = expired_list;
        expired_list = expired_list->next;
        
        // Execute callback
        if (timer->cb) {
            timer->cb(timer->arg);
        }
        
        // Handle repeating timers
        if (timer->repeat && timer->interval > 0) {
            timer->expire_time = now + timer->interval;
            
            pthread_mutex_lock(&loop->timer_mtx);
            insert_timer_sorted(loop, timer);
            pthread_mutex_unlock(&loop->timer_mtx);
        } else {
            free(timer);
        }
    }
}

static void* event_loop_worker(void *arg) {
    event_loop_t *loop = (event_loop_t *)arg;

    const int MAX_EVENTS = 64;
    struct epoll_event events[64];

    while (true) {
        int timeout = get_next_timer_timeout(loop);
        int n = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            continue;
        }

        process_expired_timers(loop);

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

    loop->next_timer_id = 0;
    loop->timer_head = NULL;
    pthread_mutex_init(&loop->timer_mtx, NULL);

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

int event_loop_add_timer(event_loop_t *loop, uint64_t delay, uint64_t interval, timer_callback_t cb, void *arg) {
    timer_zjp_t *timer = (timer_zjp_t*)malloc(sizeof(timer_zjp_t));
    if (!timer) return -1;
    timer->expire_time = get_current_time_ms() + delay;
    timer->cb = cb;
    timer->arg = arg;
    timer->repeat = interval > 0;
    timer->interval = interval;
    timer->next = NULL;

    pthread_mutex_lock(&loop->timer_mtx);
    timer->timer_id = loop->next_timer_id++;
    insert_timer_sorted(loop, timer);
    pthread_mutex_unlock(&loop->timer_mtx);

    return timer->timer_id;
}

int event_loop_del_timer(event_loop_t *loop, uint64_t timer_id) {
    pthread_mutex_lock(&loop->timer_mtx);
    timer_zjp_t **pp = &loop->timer_head;
    while (*pp) {
        if ((*pp)->timer_id == timer_id) {
            timer_zjp_t *rm = *pp;
            *pp = rm->next;
            pthread_mutex_unlock(&loop->timer_mtx);
            free(rm);
            return 0;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&loop->timer_mtx);
    return -1;
}

int event_loop_mod_timer(event_loop_t *loop, uint64_t timer_id, uint64_t new_delay, uint64_t new_interval) {
    pthread_mutex_lock(&loop->timer_mtx);
    timer_zjp_t **pp = &loop->timer_head;
    while (*pp) {
        if ((*pp)->timer_id == timer_id) {
            (*pp)->expire_time = get_current_time_ms() + new_delay;
            (*pp)->interval = new_interval;
            (*pp)->repeat = new_interval > 0;
            pthread_mutex_unlock(&loop->timer_mtx);
            return 0;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&loop->timer_mtx);
    return -1;
}

int event_loop_timer_count(event_loop_t *loop) {
    pthread_mutex_lock(&loop->timer_mtx);
    int count = 0;
    timer_zjp_t *cur = loop->timer_head;
    while (cur) {
        count++;
        cur = cur->next;
    }
    pthread_mutex_unlock(&loop->timer_mtx);
    return count;
}
