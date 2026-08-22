#include "timer_heap.h"
#include <stdlib.h>
#include <sys/time.h>
/*

在完全二叉树中，节点在数组里的下标有固定规律：
节点 index 的父亲下标：(index - 1) / 2
节点 index 的左孩子下标：index * 2 + 1
节点 index 的右孩子下标：index * 2 + 2
最小堆铁律：父亲节点的到期时间必须<=两个孩子节点

*/

void swap_nodes(TimerNode *a, TimerNode *b) {
    TimerNode temp = *a;
    *a = *b;
    *b = temp;
}

void sift_up(TimerHeap *heap, size_t index) {
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (heap->array[index].expire_time >= heap->array[parent].expire_time) {
            break;
        }
        swap_nodes(&heap->array[index], &heap->array[parent]);
        index = parent;// 继续向上浮
    }
}

void sift_down(TimerHeap *heap, size_t index) {
    size_t size = heap->size;
    while (index * 2 + 1 < size) {// 只要有左孩子就继续下沉
        size_t left = index * 2 + 1;
        size_t right = index * 2 + 2;
        size_t min_child = left;
        // 如果有右孩子，并且右孩子比左孩子更早到期，挑出较早的那个孩子
        if (right < size && heap->array[right].expire_time < heap->array[left].expire_time) {
            min_child = right;
        }
        // 如果当前节点比两个孩子都早到期，说明稳居上位，停止下沉
        if (heap->array[index].expire_time <= heap->array[min_child].expire_time) {
            break;
        }
        swap_nodes(&heap->array[index], &heap->array[min_child]);
        index = min_child;
    }
}

uint64_t get_current_time_ms(void) {
    struct timeval tv;// 获取当前时间
    gettimeofday(&tv, NULL);// 将秒和微秒转换为毫秒
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;// 返回毫秒级时间戳
}

TimerHeap* timer_heap_create(size_t capacity) {
    TimerHeap *heap = malloc(sizeof(TimerHeap));
    if (!heap) return NULL;
    heap->array = malloc(sizeof(TimerNode) * capacity);
    if (!heap->array) {
        free(heap);
        return NULL;
    }
    heap->capacity = capacity;
    heap->size = 0;
    return heap;
}

void timer_heap_destroy(TimerHeap *heap) {
    if (heap) {
        free(heap->array);
        free(heap);
    }
}

void timer_heap_add(TimerHeap *heap, uint64_t timeout_ms, timer_callback cb, void *arg) {
    if (heap->size >= heap->capacity) {
        heap->capacity *= 2;
        heap->array = realloc(heap->array, sizeof(TimerNode) * heap->capacity);
    }
    // 2. 组装闹钟数据：绝对时间 = 当前时间 + 超时时间
    TimerNode node;
    node.expire_time = get_current_time_ms() + timeout_ms;
    node.cb = cb;
    node.arg = arg;
    // 3. 把新闹钟先放到数组最末尾，然后执行 sift_up 向上浮到合适位置
    heap->array[heap->size] = node;
    sift_up(heap, heap->size);
    heap->size++;
}

int timer_heap_get_next_timeout(TimerHeap *heap) {
    if (heap->size == 0) return -1;// 没任务，让 epoll_wait 一直睡

    uint64_t now = get_current_time_ms();
    uint64_t expire = heap->array[0].expire_time;; // 瞅一眼堆顶最早到期时间

    if (now >= expire) return 0;// 已经有任务到期了，别睡了，立刻醒来
    return (int)(expire - now);// 离最近的到期还剩多少毫秒，精准睡多久
}

void timer_heap_tick(TimerHeap *heap) {
    if (heap->size == 0) return;// 每次只看堆顶
    uint64_t now = get_current_time_ms();// 堆顶都没到期，后面的肯定更没到期，直接跳出

    while (heap->size > 0) {
        TimerNode *min_node = &heap->array[0];
        if (now < min_node->expire_time) break;// 没到期，直接跳出
        // 提取堆顶的任务信息
        timer_callback cb = min_node->cb;
        void *arg = min_node->arg;
        // 把末尾元素放到堆顶，size 减 1，执行 sift_down 下沉调整
        heap->array[0] = heap->array[heap->size - 1];
        heap->size--;
        sift_down(heap, 0);
    // 执行回调函数
        if (cb) cb(arg);
    }
}