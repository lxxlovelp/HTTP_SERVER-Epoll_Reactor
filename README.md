这是一个关于多进程的http网络服务器架构

epoll_server.c方法解释

1. `Connection` 结构体
- 作用：保存一个客户端连接的所有状态。
- 里面主要有：
  - `fd`：客户端 socket 描述符
  - `request`：接收到的 HTTP 请求数据缓冲区
  - `request_len`：已接收请求长度
  - `response`：要发回客户端的响应内容
  - `response_len`：响应总长度
  - `response_sent`：已经发出去多少

2. `close_connection(int epoll_fd, Connection *conn)`
- 作用：关闭一个连接。
- 参数：
  - `epoll_fd`：epoll 实例 fd
  - `conn`：要关闭的连接对象
- 返回值：无返回值 (`void`)
- 做的事：
  - 从 epoll 中删除这个 fd
  - 关闭 socket
  - 释放响应内容内存
  - 释放 `Connection` 对象

3. `modify_connection_events(int epoll_fd, Connection *conn, uint32_t mask)`
- 作用：修改这个连接在 epoll 里的监听事件。
- 参数：
  - `epoll_fd`：epoll fd
  - `conn`：连接对象
  - `mask`：要监听的新事件类型，比如 `EPOLLIN`、`EPOLLOUT`
- 返回值：
  - `0`：成功
  - `-1`：失败
- 做的事：
  - 设置新的 epoll 事件
  - 让这个 fd 监听你指定的事件

4. `add_connection(int epoll_fd, int fd)`
- 作用：给一个新客户端 fd 创建 `Connection`，并注册到 epoll。
- 参数：
  - `epoll_fd`：epoll fd
  - `fd`：客户端 socket fd
- 返回值：
  - `0`：成功
  - `-1`：失败
- 做的事：
  - 分配 `Connection`
  - 初始化 `fd`
  - 注册为 `EPOLLIN | EPOLLRDHUP`

5. `add_fd_to_epoll(int epoll_fd, int fd)`
- 作用：把监听 socket 加进 epoll，专门用于“接受新连接”。
- 参数：
  - `epoll_fd`：epoll fd
  - `fd`：监听 socket fd
- 返回值：
  - `0`：成功
  - `-1`：失败
- 做的事：
  - 注册监听 fd
  - 监听 `EPOLLIN` 事件
  - 使用 `LISTENER_TAG` 作为标记，区分“监听 socket”还是“客户端 socket”

6. `complete_http_request(const char *buf, size_t len)`
- 作用：判断当前收到的请求是否已经完整。
- 参数：
  - `buf`：请求数据缓冲区
  - `len`：当前已收到的数据长度
- 返回值：
  - `>0`：请求已经完整，返回完整请求长度
  - `0`：请求还不完整
  - `-1`：请求无效或超过限制
- 做的事：
  - 找请求头结束符 `\r\n\r\n`
  - 检查是否有 `Content-Length`
  - 计算请求体长度
  - 判断是不是已经收齐了整个 HTTP 请求

7. `queue_response(int epoll_fd, Connection *conn, size_t request_len)`
- 作用：根据已经收到的请求，生成 HTTP 响应，并切换到可写状态。
- 参数：
  - `epoll_fd`：epoll fd
  - `conn`：当前连接对象
  - `request_len`：完整请求的长度
- 返回值：
  - `0`：成功
  - `-1`：失败
- 做的事：
  - 调用 `build_http_response(...)` 生成响应
  - 如果是 CGI handoff，则关闭连接
  - 否则把连接状态切换为 `EPOLLOUT`

8. `read_client(int epoll_fd, Connection *conn)`
- 作用：从客户端 socket 读取数据。
- 参数：
  - `epoll_fd`：epoll fd
  - `conn`：当前客户端连接
- 返回值：
  - `0`：当前暂时没有更多数据可读，或者只是暂时返回
  - `-1`：读失败/客户端断开/请求非法
  - 其他：通常由 `queue_response()` 决定，成功时返回 `0`
- 做的事：
  - 循环 `recv()`
  - 把数据追加到 `conn->request`
  - 调用 `complete_http_request()` 判断请求是否完整
  - 请求完整后调用 `queue_response()`

9. `write_client(Connection *conn)`
- 作用：把响应内容发给客户端。
- 参数：
  - `conn`：当前连接对象
- 返回值：
  - `0`：还没发完，等下一次可写事件
  - `1`：响应已经全部发送完
  - `-1`：发送失败
- 做的事：
  - 循环 `send()`
  - 把 `conn->response` 里的内容写出去
  - 直到发完为止

10. `epoll_wait_loop(int epoll_fd, int listen_fd)`
- 作用：主事件循环，整个服务器的“心脏”。
- 参数：
  - `epoll_fd`：epoll fd
  - `listen_fd`：监听 socket fd
- 返回值：
  - `-1`：发生致命错误
  - 一般不会正常返回（因为是 `for(;;)` 无限循环）
- 做的事：
  - 调用 `epoll_wait()` 等待事件
  - 如果是新连接事件：调用 `accept()` 接收客户端
  - 如果是客户端可读：调用 `read_client()`
  - 如果是客户端可写：调用 `write_client()`
  - 如果发生错误或关闭：关闭对应连接

