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

#define LISTENER_TAG UINT64_C(1)


typedef struct {
    int fd;
    char request[MAX_REQUEST_SIZE + 1];
    size_t request_len;
    char *response;
    size_t response_len;
    size_t response_sent;
} Connection;

static struct epoll_event events[MAX_EVENTS];


static void close_connection(int epoll_fd, Connection *conn)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    close(conn->fd);
    free(conn->response);
    free(conn);
}


static int modify_connection_events(int epoll_fd, Connection *conn, uint32_t mask)
{
    struct epoll_event ev = {0};
    ev.events = mask | EPOLLRDHUP;// 保留 EPOLLRDHUP 事件，避免客户端关闭连接时无法检测
    ev.data.ptr = conn;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
}



static int add_connection(int epoll_fd, int fd)
{
    Connection *conn = calloc(1, sizeof(*conn));
    if (conn == NULL) {
        return -1;
    }
    conn->fd = fd;

    struct epoll_event ev = {0};// 初始化 epoll_event 结构体
    ev.events = EPOLLIN | EPOLLRDHUP;
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


/// 解析 HTTP 请求，返回请求总长度（包括请求头和请求体），如果请求不完整返回 0，如果请求无效返回 -1
static ssize_t complete_http_request(const char *buf, size_t len)
{
    const char *header_end = NULL;

    for (size_t i = 3; i < len; ++i) {
        if (memcmp(buf + i - 3, "\r\n\r\n", 4) == 0) {
            header_end = buf + i + 1;// 指向请求头+请求行结束位置
            break;
        }
    }
    if (header_end == NULL) {// 请求头未完整接收
        if (len >= MAX_REQUEST_SIZE) {// 请求头过大，超过最大请求大小
            return -1;
        }
        return 0;
    }


    size_t header_len = (size_t)(header_end - buf);// 请求头长度
    size_t content_len = 0;// 请求体长度
    const char *line = buf;

    while (line < header_end) {

        const char *line_end = strstr(line, "\r\n");
        if (line_end == NULL || line_end >= header_end) {// 处理最后一行或异常情况
            break;
        }

        if (strncasecmp(line, "Content-Length:", 15) == 0) {// 找到 Content-Length 头部
            char *end = NULL;
            unsigned long value = strtoul(line + 15, &end, 10);// 解析请求体长度
            if (end == line + 15 || end > line_end || value > MAX_REQUEST_SIZE) {// 无效的 Content-Length
                return -1;
            }
            content_len = (size_t)value;
        }

        line = line_end + 2;// 移动到下一行
    }


    if (content_len > MAX_REQUEST_SIZE - header_len) {//
        return -1;
    }
    size_t total = header_len + content_len;
    return len >= total ? (ssize_t)total : 0;
}


static int queue_response(int epoll_fd, Connection *conn, size_t request_len)
{
    int rc = build_http_response(conn->request, request_len, conn->fd,
                             &conn->response, &conn->response_len);
    if (rc == HTTP_RESPONSE_CGI_HANDOFF) {
        /* The CGI child inherited the socket; the Reactor releases its copy. */
        close_connection(epoll_fd, conn);
        return 0;
    }
    if (rc != HTTP_RESPONSE_READY || conn->response == NULL) {
        return -1;
    }
    if (modify_connection_events(epoll_fd, conn, EPOLLOUT) == -1) {// 修改 epoll 事件为可写
        return -1;
    }
    return 0;
}


static int read_client(int epoll_fd, Connection *conn)
{
    for (;;) {
        if (conn->request_len == MAX_REQUEST_SIZE) {// 请求缓冲区已满，无法继续读取
            return -1;
        }
        ssize_t n = recv(conn->fd, conn->request + conn->request_len,
                         MAX_REQUEST_SIZE - conn->request_len, 0);
        if (n > 0) {
            conn->request_len += (size_t)n;
            conn->request[conn->request_len] = '\0';
            ssize_t request_len = complete_http_request(conn->request, conn->request_len);
            if (request_len < 0) {
                return -1;
            }
            if (request_len > 0) {
                return queue_response(epoll_fd, conn, (size_t)request_len);
            }
            continue;
        }
        if (n == 0) {
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {// 非阻塞套接字没有数据可读，等待下一次可读事件
            return 0;
        }
        if (errno != EINTR) {// 如果不是被信号中断，打印错误信息并返回 -1
            perror("recv");
            return -1;
        }
    }
}



static int write_client(Connection *conn)
{
    while (conn->response_sent < conn->response_len) {
        ssize_t n = send(conn->fd, conn->response + conn->response_sent,
                         conn->response_len - conn->response_sent, MSG_NOSIGNAL);
        if (n > 0) {
            conn->response_sent += (size_t)n;
            continue;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        if (n == -1 && errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 1;
}


int epoll_wait_loop(int epoll_fd, int listen_fd)
{
    (void)listen_fd;
    for (;;) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
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
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno != EINTR) perror("accept");
                        break;
                    }
                    if (set_nonblock(client_fd) == -1 || add_connection(epoll_fd, client_fd) == -1) {// 添加新连接到 epoll
                        perror("add client to epoll");
                        close(client_fd);
                    }
                }
                continue;
            }

            Connection *conn = events[i].data.ptr;
            uint32_t ev = events[i].events;
            if (ev & (EPOLLERR | EPOLLHUP)) {
                close_connection(epoll_fd, conn);
            } else if (ev & EPOLLIN) {
                if (read_client(epoll_fd, conn) == -1) close_connection(epoll_fd, conn);
            } else if (ev & EPOLLOUT) {
                int written = write_client(conn);
                if (written != 0) close_connection(epoll_fd, conn);
            } else if (ev & EPOLLRDHUP) {
                close_connection(epoll_fd, conn);
            }
        }
    }
}
