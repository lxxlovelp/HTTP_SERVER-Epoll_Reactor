#include "timer_heap.h"
#include <stdlib.h>
#include <sys/time.h>

static void swap_nodes(TimerNode *a, TimerNode *b) {
    TimerNode temp = *a;
    *a = *b;
    *b = temp;
}

static void sift_up(TimerHeap *heap, size_t index) {
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (heap->array[index].expire_time >= heap->array[parent].expire_time) {
            break;
        }
        swap_nodes(&heap->array[index], &heap->array[parent]);
        index = parent;
    }
}

static void sift_down(TimerHeap *heap, size_t index) {
    size_t size = heap->size;
    while (index * 2 + 1 < size) {
        size_t left = index * 2 + 1;
        size_t right = index * 2 + 2;
        size_t min_child = left;

        if (right < size && heap->array[right].expire_time < heap->array[left].expire_time) {
            min_child = right;
        }
        if (heap->array[index].expire_time <= heap->array[min_child].expire_time) {
            break;
        }
        swap_nodes(&heap->array[index], &heap->array[min_child]);
        index = min_child;
    }
}

uint64_t get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
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
    TimerNode node;
    node.expire_time = get_current_time_ms() + timeout_ms;
    node.cb = cb;
    node.arg = arg;

    heap->array[heap->size] = node;
    sift_up(heap, heap->size);
    heap->size++;
}

int timer_heap_get_next_timeout(TimerHeap *heap) {
    if (heap->size == 0) return -1;

    uint64_t now = get_current_time_ms();
    uint64_t expire = heap->array[0].expire_time;

    if (now >= expire) return 0;
    return (int)(expire - now);
}

void timer_heap_tick(TimerHeap *heap) {
    if (heap->size == 0) return;
    uint64_t now = get_current_time_ms();

    while (heap->size > 0) {
        TimerNode *min_node = &heap->array[0];
        if (now < min_node->expire_time) break;

        timer_callback cb = min_node->cb;
        void *arg = min_node->arg;

        heap->array[0] = heap->array[heap->size - 1];
        heap->size--;
        sift_down(heap, 0);

        if (cb) cb(arg);
    }
}