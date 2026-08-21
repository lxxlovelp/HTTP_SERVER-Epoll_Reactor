#ifndef TIMER_HEAP_H
#define TIMER_HEAP_H

#include <stdint.h>
#include <stddef.h>

typedef void (*timer_callback)(void *arg);

typedef struct {
    uint64_t expire_time; // 绝对过期时间戳（毫秒）
    timer_callback cb;    // 到期回调
    void *arg;            // 回调参数
} TimerNode;

typedef struct {
    TimerNode *array;
    size_t capacity;
    size_t size;
} TimerHeap;

uint64_t get_current_time_ms(void);
TimerHeap* timer_heap_create(size_t capacity);
void timer_heap_destroy(TimerHeap *heap);
void timer_heap_add(TimerHeap *heap, uint64_t timeout_ms, timer_callback cb, void *arg);
int timer_heap_get_next_timeout(TimerHeap *heap);
void timer_heap_tick(TimerHeap *heap);

#endif