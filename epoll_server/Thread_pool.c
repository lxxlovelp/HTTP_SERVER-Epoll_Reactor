#include "Thread_pool.h"
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
/*################################################################3
Create multiple worker threads in advance
→ the main thread keeps adding tasks to the task queue
→ worker threads take tasks from the queue and execute them
→ when destroying the thread pool, notify all threads to exit.
##################################################################*/

static void *worker_thread_routine(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);

        // 没有任务且线程池未关闭：进入等待
        while (pool->queue_head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->notify, &pool->lock);
        }

        // 已关闭，并且任务也处理完了：退出线程
        if (pool->shutdown && pool->queue_head == NULL) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        // 从队头取一个任务
        Task *task = pool->queue_head;
        pool->queue_head = task->next;
        // 取走最后一个任务后，队尾也要置空
        if (pool->queue_head == NULL) {
            pool->queue_tail = NULL;
        }

        pthread_mutex_unlock(&pool->lock);

        // 执行任务时不要持有锁，否则其他线程无法取任务/添加任务
        task->function(task->arg);
        free(task);
    }

    return NULL;
}

ThreadPool* threadpool_create(int thread_count) {
    if (thread_count <= 0) {
        thread_count = 4;
    }// If the number of threads is not specified, default to 4 threads

    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    if (pool == NULL) {
        return NULL;
    }

    pool->thread_count = thread_count;
    pool->queue_head = NULL;
    pool->queue_tail = NULL;
    pool->shutdown = 0;

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->notify, NULL);

    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);// Allocate memory for thread IDs
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    for (int i = 0; i < thread_count; i++) {
        // 这样工作线程才能拿到同一个线程池对象，访问共享的任务队列、互斥锁、条件变量等：
        if (pthread_create(&pool->threads[i], NULL, worker_thread_routine, (void *)pool) != 0) {
            threadpool_destroy(pool);
            return NULL;
        }
    }

    printf("[ThreadPool] 线程池创建成功，工作线程数量: %d\n", thread_count);
    return pool;
}



int threadpool_add_task(ThreadPool *pool, thread_func_t function, void *arg) {
    if (!pool || !function) return -1;

    Task *task = (Task *)malloc(sizeof(Task));
    if (!task) return -1;

    task->function = function;
    task->arg = arg;
    task->next = NULL;

    pthread_mutex_lock(&pool->lock);

    if (pool->shutdown) {// If the thread pool is shutting down, do not add new tasks
        pthread_mutex_unlock(&pool->lock);
        free(task);
        return -1;
    }

    if (pool->queue_tail == NULL) {
        pool->queue_head = task;
        pool->queue_tail = task;
    } else {
        pool->queue_tail->next = task;
        pool->queue_tail = task;
    }

    // 唤醒一个休眠中的 Worker 线程
    pthread_cond_signal(&pool->notify);
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

int threadpool_destroy(ThreadPool *pool) {
    if (!pool) return -1;

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->notify);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);

    Task *cur = pool->queue_head;//
    while (cur) {
        Task *next = cur->next;
        free(cur);
        cur = next;
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->notify);
    free(pool);
    return 0;
}
