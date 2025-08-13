#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "event_loop.h"

static void* event_loop_worker(void *arg) {
    event_loop_t *loop = (event_loop_t *)arg;

    while (true) {
        pthread_mutex_lock(&loop->event_loop_mtx);
        while (loop->size == 0) {
            pthread_cond_wait(&loop->event_loop_cond, &loop->event_loop_mtx);
        }
        task_t *task = loop->task_head;
        if (task) {
            loop->task_head = task->next;
            if (loop->task_head == NULL) {
                loop->task_tail = NULL;
            }
            loop->size--;
        }
        loop->event_loop_state = EV_BUSY;
        pthread_mutex_unlock(&loop->event_loop_mtx);
        task->ev_cb(task->arg);
        free(task);

        pthread_mutex_lock(&loop->event_loop_mtx);
        if (loop->size == 0) {
            loop->event_loop_state = EV_READY;
        }
        pthread_mutex_unlock(&loop->event_loop_mtx);
    }

    return NULL;
}

void event_loop_init(event_loop_t *loop) {
    loop->task_head = loop->task_tail = NULL;
    loop->size = 0;
    loop->event_loop_state = EV_READY;
    pthread_mutex_init(&loop->event_loop_mtx, NULL);
    pthread_cond_init(&loop->event_loop_cond, NULL);
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
    pthread_mutex_unlock(&loop->event_loop_mtx);
    pthread_cond_signal(&loop->event_loop_cond);
}

size_t event_loop_task_size(event_loop_t *loop) {
    pthread_mutex_lock(&loop->event_loop_mtx);
    size_t size = loop->size;
    pthread_mutex_unlock(&loop->event_loop_mtx);
    return size;
}
