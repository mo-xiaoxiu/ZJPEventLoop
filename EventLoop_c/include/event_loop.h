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

/* event loop list */
struct _event_loop {
    struct _task *task_head, *task_tail;
    size_t size;
    _event_loop_state event_loop_state;
    pthread_t event_thread;
    pthread_mutex_t event_loop_mtx;
    pthread_cond_t event_loop_cond;
};

typedef struct _task task_t;
typedef struct _event_loop event_loop_t;

void event_loop_init(event_loop_t *loop);

void event_loop_run(event_loop_t *loop);

void event_loop_add_task(event_loop_t *loop, event_callback_t cb, void *arg);

size_t event_loop_task_size(event_loop_t *loop);

#endif
