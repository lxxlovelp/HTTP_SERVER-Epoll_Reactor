#ifndef Functon_H

#define Functon_H
#include "epoll_server.h"
#include "timer_heap.h"


int queue_response(int epoll_fd, Connection *conn, size_t request_len);
void add_or_refresh_timer(TimerHeap *heap,int epoll_fd,Connection *conn);
void *queue_response_task(void *arg);
int read_client(int epoll_fd, Connection *conn,ThreadPool *pool,TimerHeap *heap);
int write_client(int epoll_fd,Connection *conn);
int dispatch_task(int epoll_fd, Connection *conn, size_t request_len, ThreadPool *pool);

#endif
