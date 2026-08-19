#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <pthread.h>
typedef void* (*thread_func_t) (void*arg);


typedef struct Task {
    thread_func_t function;
    void *arg;
    struct Task *next;
} Task;

/* 0: running, 1: shutting down
/Head of queue: the next task to be executed
  Tail of queue: the last task added*/
typedef struct ThreadPool {
    pthread_mutex_t lock;
    pthread_cond_t notify;
    pthread_t *threads;
    Task *queue_head;
    Task *queue_tail;
    int thread_count;
    int shutdown;
} ThreadPool;

ThreadPool* threadpool_create(int thread_count);
int threadpool_add_task(ThreadPool *pool, thread_func_t function, void *arg);
int threadpool_destroy(ThreadPool *pool);

#endif 