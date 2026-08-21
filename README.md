## HTTP Server 架构说明

本项目实现了一个基于 Epoll Reactor 模式与线程池的 C/C++ 高并发 HTTP 服务器。为了保证多线程环境下的内存安全与极高的吞吐量，系统设计遵循严格的事件驱动与职责分离原则。

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



### 1. 核心状态机机制 (EPOLLONESHOT)

* **事件单次屏蔽机制**：所有建立连接的套接字必须注册 `EPOLLONESHOT` 标志，确保同一时间只有一个工作线程操作该 Socket，从根本上杜绝多线程读写竞态。


* **手动状态重置（上膛）**：当读写函数因非阻塞返回 `EAGAIN` 时，或者工作线程完成业务处理时，必须显式调用 `epoll_ctl` 配合 `EPOLL_CTL_MOD` 重新激活 Epoll 监听，否则会导致连接永久挂死。


* **I/O 职责分离**：主循环（Reactor）严格独占网络 `recv` 与 `send` 操作，工作线程仅负责纯 CPU 计算的报文解析与响应构建，切勿将网络发送任务投递至线程池。



### 2. 关键 Bug 修复与避坑记录

* **修复文件描述符串话（FD Hijacking）**：严禁在主线程中直接 `close(fd)`。必须将关闭文件描述符的操作移至原子引用计数归零的终态函数中，防止被系统立即复用给新连接而引发数据错乱。


* **消除死锁挂死**：修复了在读写尚未完成时（`EWOULDBLOCK`），因未重新注册监听事件而导致整个 Socket 失联的逻辑断层。


* **修复线程池崩溃与内存泄漏**：在初始化时改用 `calloc` 分配线程数组以防 `pthread_join` 野指针；在生成响应失败的异常分支中，必须主动回收连接资源以防止孤儿连接耗尽系统内存。



### 3. 连接生命周期管理

* **原子引用计数保命**：连接上下文内部采用 `atomic_int ref` 记录当前被线程池与主线程持有的引用数量，防止超时清理或异常断开时触发 Double Free 或野指针访问。


* **优雅的摘除机制**：当检测到客户端断开或产生错误时，第一步必须是调用 `EPOLL_CTL_DEL` 将其从监听树上摘除，阻断后续所有无效唤醒。



---

你在接下来的压测或长连接功能开发中，还需要我帮你梳理哪部分的设计思路吗？
## 清理

```bash
make clean
```
