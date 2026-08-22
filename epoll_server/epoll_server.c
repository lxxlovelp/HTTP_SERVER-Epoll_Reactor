#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "../network/http.h"
#include "../network/socket.h"
#include "epoll_server.h"
#include "Thread_pool.h"
#include <sys/eventfd.h>
#include <stdatomic.h>
#include "timer_heap.h"
#include "function.h"
#include <stdint.h>

static struct epoll_event events[MAX_EVENTS];

void release_connection(Connection *conn)
{
    if (atomic_fetch_sub(&conn->ref,1) == 1)
    {
        close(conn->fd);
        free(conn->response);
        free(conn);
    }
}


 void close_connection(int epoll_fd, Connection *conn)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    release_connection(conn);
}


int add_connection(int epoll_fd, int fd,TimerHeap *heap)
{
    Connection *conn = calloc(1, sizeof(*conn));
    if (conn == NULL) {
        return -1;
    }
    conn->fd = fd;
    atomic_init(&conn->ref, 1);   // 初始引用：epoll线程持有
    // 新连接接入立即开启 15 秒超时倒计时
    add_or_refresh_timer(heap, epoll_fd, conn);
    struct epoll_event ev = {0};// 初始化 epoll_event 结构体
    ev.events = EPOLLIN | EPOLLRDHUP |EPOLLONESHOT;
    ev.data.ptr = conn;// 将 Connection 结构体指针存储在 epoll_event 的 data.ptr 中，以便在事件触发时访问连接状态
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        free(conn);
        return -1;
    }
    return 0;
}

int add_fd_to_epoll(int epoll_fd, int fd)
{
    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.u64 = LISTENER_TAG;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl ADD");
        return -1;
    }
    return 0;
}


int modify_connection_events(int epoll_fd, Connection *conn, uint32_t mask)
{
    struct epoll_event ev = {0};
    ev.events = mask | EPOLLRDHUP;// 保留 EPOLLRDHUP 事件，避免客户端关闭连接时无法检测
    ev.data.ptr = conn;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}


int epoll_wait_loop(int epoll_fd, int listen_fd,ThreadPool* pool)
{   
    (void)listen_fd;
    TimerHeap *timer_heap = timer_heap_create(1024);
    if (!timer_heap) {
        fprintf(stderr, "Failed to create timer heap\n");
        return -1;
    }
    
    for (;;) {
        // 根据堆顶任务计算最近一个超时的剩余毫秒数
        int timeout_ms = timer_heap_get_next_timeout(timer_heap);
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            return -1;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.u64 == LISTENER_TAG) {// 监听套接字可读，表示有新连接
                for (;;) {
                    int client_fd = accept(listen_fd, NULL, NULL);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;// 没有更多连接可接受，退出循环
                        if (errno != EINTR) perror("accept");
                        break;
                    }
                    if  (set_nonblock(client_fd) == -1 || add_connection(epoll_fd, client_fd, timer_heap) == -1) {// 添加新连接到 epoll
                        perror("add client to epoll");
                        close(client_fd);
                    }
                }
                continue;
            }

            Connection *newconn = events[i].data.ptr;

            uint32_t ev = events[i].events;
            if (ev & (EPOLLERR)) {
                close_connection(epoll_fd, newconn);
            } else if (ev & EPOLLIN) {
                if (read_client(epoll_fd, newconn,pool,timer_heap) == -1) {
                    close_connection(epoll_fd, newconn);
                }
            } else if (ev & EPOLLOUT) {
                int written = write_client(epoll_fd,newconn);
                if (written==1) {
                    if (newconn->keep_alive) {
                     // 释放本次已发出的响应报文
                        free(newconn->response);
                        newconn->response = NULL;

                        // 粘包平移：将缓冲区里未处理的后续请求往前挪
                        size_t remaining = newconn->request_len - newconn->parsed_len;
                        if (remaining > 0) {
                            memmove(newconn->request, newconn->request + newconn->parsed_len, remaining);
                        }
                        newconn->request_len = remaining;
                        newconn->request[remaining] = '\0';
                        newconn->parsed_len = 0;
                        newconn->response_len = 0;
                        newconn->response_sent = 0;

                        // 发完后刷新定时器
                        add_or_refresh_timer(timer_heap, epoll_fd, newconn);

                        // 检查平移后的数据是否已包含完整的下一条 HTTP 请求
                        ssize_t next_req_len = complete_http_request(newconn->request, newconn->request_len);
                        if (next_req_len > 0) {
                            if (dispatch_task(epoll_fd, newconn, (size_t)next_req_len, pool) != 0) {
                                close_connection(epoll_fd, newconn);
                            }
                        } else {
                            // 重新上膛监听 EPOLLIN
                            struct epoll_event ev_reset = {0};
                            ev_reset.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
                            ev_reset.data.ptr = newconn;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, newconn->fd, &ev_reset);
                        }
                    }
                    else{
                        // 【短连接逻辑】：对方要求断开，直接关闭
                        close_connection(epoll_fd, newconn);
                    }
                }
                else if (written != 0){
                     close_connection(epoll_fd, newconn);
                }
            } else if (ev & EPOLLRDHUP) {
                close_connection(epoll_fd, newconn);
            }
        }
        timer_heap_tick(timer_heap);// 处理到期任务
    }

    timer_heap_destroy(timer_heap);
    return 0;

}
