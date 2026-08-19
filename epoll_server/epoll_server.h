#ifndef EPOLL_SERVER_H
#define EPOLL_SERVER_H

#include "Thread_pool.h"
#include <stddef.h>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define MAX_REQUEST_SIZE (64 * 1024)

/*
 * The listening socket uses a tagged epoll value.  Client sockets keep all
 * connection state in a Connection object and use EPOLLIN/EPOLLOUT as a small
 * Reactor state machine.
 */
int add_fd_to_epoll(int epoll_fd, int fd);
int epoll_wait_loop(int epoll_fd, int listen_fd,ThreadPool* pool);

#endif
