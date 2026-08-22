#ifndef EPOLL_SERVER_H
#define EPOLL_SERVER_H

#include "Thread_pool.h"
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include "timer_heap.h"
#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define LISTENER_TAG UINT64_C(1)
#define KEEPALIVE_TIMEOUT_MS 15000 // 15秒空闲超时
#define MAX_REQUEST_SIZE (64 * 1024)

 typedef struct {
    int fd;
    atomic_int ref;// 连接引用计数，确保在多线程环境下安全释放资源
    size_t parsed_len;  // 本次已消费处理的请求字节数（供粘包平移）
    char request[MAX_REQUEST_SIZE + 1];
    size_t request_len;
    char *response;
    size_t response_len;
    size_t response_sent;
    int keep_alive;
    uint64_t last_active_time;// 毫秒级最后活跃时间戳
} Connection;

typedef struct {
    int epoll_fd;
    Connection *conn;
    size_t request_len;
} QueueResponseTask;

// 供定时器回调传递的上下文
typedef struct {
    int epoll_fd;
    Connection *conn;
} TimerContext;
/*
 * The listening socket uses a tagged epoll value.  Client sockets keep all
 * connection state in a Connection object and use EPOLLIN/EPOLLOUT as a small
 * Reactor state machine.
 */

int add_fd_to_epoll(int epoll_fd, int fd);
int epoll_wait_loop(int epoll_fd, int listen_fd,ThreadPool* pool);
int add_connection(int epoll_fd, int fd,TimerHeap *heap);
void close_connection(int epoll_fd, Connection *conn);
void release_connection(Connection *conn);
int modify_connection_events(int epoll_fd, Connection *conn, uint32_t mask);
#endif
