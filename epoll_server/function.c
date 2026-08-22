#include "epoll_server.h"
#include "../network/http.h"
#include "Thread_pool.h"
#include <sys/eventfd.h>  
#include <sys/socket.h>
#include "function.h"
#include "timer_heap.h"
#include <sys/epoll.h>
#include <unistd.h>
#include "errno.h"
#include <stdlib.h>
#include <stdio.h>

int queue_response(int epoll_fd, Connection *conn, size_t request_len)
{
    int rc = build_http_response(conn->request, request_len, conn->fd,
                             &conn->response, &conn->response_len,&conn->keep_alive);
    if (rc == HTTP_RESPONSE_CGI_HANDOFF) {// 如果是 CGI 处理，直接关闭连接
        close_connection(epoll_fd, conn);
        return 0;
    }
    // 如果构建响应失败，直接关闭连接
    if (rc != HTTP_RESPONSE_READY || conn->response == NULL) {
        return -1;
    }
    // 替换原本的 modify_connection_events 调用
    // 重新注册该 fd 的 EPOLLOUT 事件，并必须带上 EPOLLONESHOT 重新激活 epoll 监听
    struct epoll_event ev = {0};
    ev.events = EPOLLOUT | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = conn;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev) == -1) {
        return -1;
    }
    return 0;
}


 void *queue_response_task(void *arg)
{
    QueueResponseTask *task = arg;

    if (queue_response(task->epoll_fd, task->conn, task->request_len) == -1) {
        // 正确的做法：工作线程发现错误，替主线程执行 close_connection 
        close_connection(task->epoll_fd, task->conn); 
    }
    release_connection(task->conn);
    free(task);
    return NULL;
}

// 刷新最后活跃时间，并向最小堆中压入一个新定时器
int read_client(int epoll_fd, Connection *conn,ThreadPool *pool,TimerHeap *heap)
{
    for (;;) {
        if (conn->request_len == MAX_REQUEST_SIZE) {// 请求缓冲区已满，无法继续读取
            return -1;
        }
        ssize_t n = recv(conn->fd, conn->request + conn->request_len,
                         MAX_REQUEST_SIZE - conn->request_len, 0);// 从客户端套接字读取数据到请求缓冲区
        if (n > 0) {
            conn->request_len += (size_t)n;
            conn->request[conn->request_len] = '\0';
            // 收到数据，刷新连接定时器
            add_or_refresh_timer(heap, epoll_fd, conn);
            
            ssize_t request_len = complete_http_request(conn->request, conn->request_len);
            if (request_len < 0) {
                return -1;
            }
            if (request_len > 0) {

                QueueResponseTask *task = malloc(sizeof(*task));
                    if (task == NULL) {
                        return -1;
                    }
                    task->epoll_fd = epoll_fd;
                    task->conn = conn; 
                    task->request_len = (size_t)request_len;
                    atomic_fetch_add(&conn->ref, 1);   // worker 增加一个引用

                if (threadpool_add_task(pool,  queue_response_task, task) != 0) {
                    atomic_fetch_sub(&conn->ref, 1); // 添加失败，撤销引用
                    free(task);
                    return -1;
                }
                return 0;                // return queue_response(epoll_fd, conn, (size_t)request_len);
            }
            continue;// 请求不完整，继续读取
        }
        if (n == 0) {
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {// 非阻塞套接字没有数据可读，等待下一次可读事件
            struct epoll_event ev = {0};
            ev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
            ev.data.ptr = conn;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
            return 0;
        }
        if (errno != EINTR) {// 如果不是被信号中断，打印错误信息并返回 -1
            perror("recv");
            return -1;
        }
    }
}


 int write_client(int epoll_fd,Connection *conn)
{
    while (conn->response_sent < conn->response_len) {
        ssize_t n = send(conn->fd, conn->response + conn->response_sent,
                         conn->response_len - conn->response_sent, MSG_NOSIGNAL);
        if (n > 0) {
            conn->response_sent += (size_t)n;
            continue;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {// 非阻塞套接字无法立即发送数据，等待下一次可写事件
            // 【补上这段重新监听的代码】
            struct epoll_event ev = {0};
            ev.events = EPOLLOUT | EPOLLRDHUP | EPOLLONESHOT;
            ev.data.ptr = conn;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
            return 0;
        }
        if (n == -1 && errno == EINTR) {// 被信号中断，继续发送
            continue;
        }
        return -1;
    }
    return 1;
}

// 定时器回调函数：检查连接是否超时，如果超时则关闭连接
void connection_timeout_cb(void *arg)
{
    TimerContext *ctx = (TimerContext *)arg;
    int epoll_fd = ctx->epoll_fd;
    Connection *conn = ctx->conn;
    free(ctx);

    uint64_t now = get_current_time_ms();
    // 真实超时检查：距离上次活跃已达到或超过阈值
    if (now - conn->last_active_time >= KEEPALIVE_TIMEOUT_MS) {
        close_connection(epoll_fd, conn);
    }
    // 无论是否真实超时，归还定时器持有的这 1 份引用
    release_connection(conn);
}


// 刷新最后活跃时间，并向最小堆中压入一个新定时器
void add_or_refresh_timer(TimerHeap *heap, int epoll_fd, Connection *conn)
{
    conn->last_active_time = get_current_time_ms();
    TimerContext *ctx = malloc(sizeof(*ctx));
    if (!ctx) return;

    ctx->epoll_fd = epoll_fd;
    ctx->conn = conn;

    atomic_fetch_add(&conn->ref, 1); // 定时器持有 1 份引用
    timer_heap_add(heap, KEEPALIVE_TIMEOUT_MS, connection_timeout_cb, ctx);
}

// 任务分发函数：将请求分发给工作线程处理
int dispatch_task(int epoll_fd, Connection *conn, size_t request_len, ThreadPool *pool)
{
    conn->parsed_len = request_len; // 记录消费长度，供平移使用
    QueueResponseTask *task = malloc(sizeof(*task));
    if (task == NULL) {
        return -1;
    }
    task->epoll_fd = epoll_fd;
    task->conn = conn;
    task->request_len = request_len;

    atomic_fetch_add(&conn->ref, 1); // worker 线程增加 1 份引用

    if (threadpool_add_task(pool, queue_response_task, task) != 0) {
        atomic_fetch_sub(&conn->ref, 1);
        free(task);
        return -1;
    }
    return 0;
}