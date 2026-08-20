#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <signal.h>
#include "network/socket.h"
#include "epoll_server/epoll_server.h"
#include "epoll_server/Thread_pool.h"

#define SERVER_PORT 12345
int main(int argc, char *argv[])
{
    /* A peer can close before an EPOLLOUT response is sent. */
    signal(SIGPIPE, SIG_IGN);// 忽略 SIGPIPE 信号
    signal(SIGCHLD, SIG_IGN);// 忽略 SIGCHLD 信号，防止僵尸进程产生
    uint16_t port = SERVER_PORT;//默认端口号

    if (argc > 1) {
        char *end;
	    port = strtoul(argv[1], &end, 10);//将字符串转换为无符号长整型数，并将结果存储在port中
	        if (*end != 0) {
		        fprintf(stderr, "port format error!\n");//检查端口号格式是否正确
		        return -1;
	    }
    }

    int listen_fd = init_listen_server(port);
    if (listen_fd < 0) return 1;

    if (set_nonblock(listen_fd) == -1) {
        close(listen_fd);
        return 1;
    }

    int epoll_fd = epoll_create1(0);//创建一个epoll实例，返回epoll文件描述符
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        return 1;
    }

   
    if (add_fd_to_epoll(epoll_fd, listen_fd) == -1) {
        close(epoll_fd);
        close(listen_fd);
        return 1;
    }
    
    ThreadPool* threadpool = threadpool_create(4);//创建一个线程池，包含4个工作线程

    /* 进入 epoll 主循环（函数在 epoll_server.c 中实现） */
    epoll_wait_loop(epoll_fd, listen_fd,threadpool);

    threadpool_destroy(threadpool);

    close(epoll_fd);
    close(listen_fd);
    return 0;
    
}
