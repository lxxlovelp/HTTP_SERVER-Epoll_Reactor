# Epoll HTTP Server Project

一个基于 Linux `epoll` 的轻量级 HTTP 服务器练习项目。服务器使用非阻塞 socket 与 Reactor 事件循环处理多个客户端连接，并实现静态文件响应、传感器数据上传和 CGI 查询流程。

> 当前状态：`Thread_pool.c/.h` 已实现基础线程池；线程池已在 `main.c` 创建并传入事件循环，但 HTTP 请求尚未安全地提交给线程池处理。

## 功能

- 基于 `epoll` 的 I/O 多路复用和非阻塞 socket
- 分段接收 HTTP 请求，并通过 `Content-Length` 判断请求是否完整
- `GET /static/<文件名>`：返回 `Resource/` 中的静态文件
- `POST /upload`：解析 JSON 传感器数据并发送到 System V 消息队列
- `GET /sensor/sensor.cgi`：启动 CGI 查询流程
- 支持 `EPOLLOUT` 分段发送响应，以及 `EPOLLRDHUP` 断开检测
- 提供基于链表任务队列、互斥锁和条件变量的线程池

## 目录结构

```text
Http_Server_Project/
├── main.c                    # 程序入口
├── Makefile                  # 构建规则
├── network/
│   ├── socket.c/.h            # 监听 socket 与非阻塞设置
│   └── http.c/.h              # HTTP 路由与响应构造
├── epoll_server/
│   ├── epoll_server.c/.h      # epoll 事件循环和连接状态
│   └── Thread_pool.c/.h       # 线程池和任务队列
├── Tool/                      # JSON、消息队列及进程工具
├── CGI/cgi_query.c            # CGI 查询程序
└── Resource/preview.html      # 示例静态页面
```

## 请求处理流程

```text
客户端请求
   ↓
监听 socket → accept()
   ↓
epoll_wait()
   ├─ EPOLLIN  → recv() → 请求完整 → 构造 HTTP 响应
   ├─ EPOLLOUT → send() → 响应发送完成
   └─ HUP / ERR / RDHUP → 关闭连接并释放 Connection
```

每个客户端连接使用一个 `Connection` 保存 socket、请求缓冲区、响应缓冲区及发送进度。

## 构建

依赖：Linux、GCC、Make、pthread、cJSON。当前 `Makefile` 中的 cJSON 源码路径为：

```text
/home/xingxinliao/lab/cJSON/cJSON.c
```

```bash
cd project/Http_Server_Project
make
```

线程池使用 pthread。若链接报 `pthread_create` 未定义，在 `Makefile` 中将：

```makefile
LDLIBS = -lm
```

改为：

```makefile
LDLIBS = -lm -pthread
```

## 运行与测试

```bash
# 默认监听 12345 端口
./epoll_server_project

# 指定端口
./epoll_server_project 8080
```

```bash
# 静态文件
curl -i http://127.0.0.1:12345/static/preview.html

# 上传传感器 JSON
curl -i -X POST http://127.0.0.1:12345/upload \
  -H 'Content-Type: application/json' \
  -d '{"Temperature":25.5,"Humidity":60,"Light":120,"CO₂":450}'

# CGI 查询
curl -i http://127.0.0.1:12345/sensor/sensor.cgi
```

## 线程池

线程池任务队列是单向链表：

```text
queue_head                            queue_tail
    ↓                                     ↓
[Task] → [Task] → [Task] → NULL
```

- `threadpool_add_task()`：在队尾添加任务，唤醒工作线程。
- 工作线程：从队头取任务，并执行 `task->function(task->arg)`。
- `threadpool_destroy()`：通知工作线程退出，并释放未执行任务。

将 HTTP 处理接入线程池时，不能直接在工作线程释放或无保护地访问 `Connection`。安全方案应由工作线程完成耗时计算，再用 `eventfd` 或完成队列通知 epoll 主线程切换到 `EPOLLOUT`。

## 当前限制

- 暂未支持 Keep-Alive、HTTP pipelining 与 chunked 编码。
- 静态文件根目录、cJSON 路径使用绝对路径。
- CGI 与 IPC 依赖本机的 FIFO、共享内存和信号量环境。
- 线程池尚未完成与 HTTP Reactor 的安全集成。

## 清理

```bash
make clean
```
