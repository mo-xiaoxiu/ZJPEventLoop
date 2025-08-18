#ifndef _EVENT_LOOP_
#define _EVENT_LOOP_

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef void (*_event_callback)(void*);
typedef _event_callback event_callback_t;

typedef enum {
    EV_READY,
    EV_BUSY,
}_event_loop_state;

/* task */
struct _task {
    struct _task *next;
    _event_callback ev_cb;
    void *arg;
};

/* timer manager*/
typedef void (*_timer_callback)(void* arg);
typedef _timer_callback timer_callback_t;

struct _timer {
    uint64_t timer_id;
    uint64_t expire_time;
    uint64_t interval;
    timer_callback_t cb;
    void *arg;
    bool repeat;
    struct _timer *next;
};

/* io callback: fd and events will be provided */
typedef void (*_io_callback)(int fd, unsigned int events, void *arg);
typedef _io_callback io_callback_t;

/* io watcher node */
struct _io_watcher {
    int fd;
    unsigned int events; /* EPOLLIN | EPOLLOUT | ... */
    io_callback_t cb;
    void *arg;
    struct _io_watcher *next;
};

/* event loop list */
struct _event_loop {
    struct _task *task_head, *task_tail;
    size_t size;
    _event_loop_state event_loop_state;
    pthread_t event_thread;
    pthread_mutex_t event_loop_mtx;
    pthread_cond_t event_loop_cond; /* kept for compatibility */

    /* io multiplexer */
    int epoll_fd; /* epoll instance */
    int event_fd; /* eventfd to wake epoll on task arrival */
    struct _io_watcher *io_head; /* registered io watchers */

    /* timer manager */
    struct _timer *timer_head;
    pthread_mutex_t timer_mtx;
    uint64_t next_timer_id;
};

typedef struct _task task_t;
typedef struct _event_loop event_loop_t;
typedef struct _timer timer_zjp_t;

void event_loop_init(event_loop_t *loop);

void event_loop_run(event_loop_t *loop);

void event_loop_add_task(event_loop_t *loop, event_callback_t cb, void *arg);

/* I/O watcher management. 'events' use epoll flags (EPOLLIN, EPOLLOUT, etc.) */
int event_loop_add_io(event_loop_t *loop, int fd, unsigned int events, io_callback_t cb, void *arg);
int event_loop_mod_io(event_loop_t *loop, int fd, unsigned int events);
int event_loop_del_io(event_loop_t *loop, int fd);

/* timer management */
int event_loop_add_timer(event_loop_t *loop, uint64_t delay, uint64_t interval, timer_callback_t cb, void *arg);
int event_loop_mod_timer(event_loop_t *loop, uint64_t timer_id, uint64_t new_delay, uint64_t new_interval);
int event_loop_del_timer(event_loop_t *loop, uint64_t timer_id);
int event_loop_timer_count(event_loop_t *loop);

size_t event_loop_task_size(event_loop_t *loop);

#endif
