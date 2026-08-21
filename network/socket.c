#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

//设置非阻塞
int set_nonblock(int fd)
{

    int flag = fcntl(fd,F_GETFL);//获取文件描述符的状态标志

    if(flag == -1)
    {
        perror("fcntl");
        return -1;
    }

    flag |= O_NONBLOCK;//设置文件描述符为非阻塞模式

    if(fcntl(fd,F_SETFL,flag)==-1)//设置文件描述符的状态标志
    {
        perror("fcntl");
        return -1;
    }
    return 0;
}

int init_listen_server(uint16_t port)
{

    int tcp_socket = socket(AF_INET, SOCK_STREAM,0);//创建一个TCP套接字，返回套接字描述符

    if(tcp_socket == -1)
    {
        perror("socket");
        return -1;
    }
    // 地址复用,地址复用（通常用 SO_REUSEADDR）是给 bind() 用的，主要作用是，允许一个端口在 socket 还处于 TIME_WAIT 状态时被重新绑定
    int reuse=1;

    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse,sizeof(reuse));

    
    struct sockaddr_in self;//创建一个sockaddr_in结构体变量self，用于存储服务器的IP地址和端口号

    memset(&self,0,  sizeof(self));


    self.sin_family = AF_INET;

    self.sin_port = htons(port);//

    self.sin_addr.s_addr =htonl(INADDR_ANY);//将IP地址设置为INADDR_ANY，表示服务器可以接受任意IP地址的连接请求


    int ret = bind(tcp_socket, (struct sockaddr*)&self, sizeof(self));//绑定套接字到指定的IP地址和端口号


    if(ret==-1)
    {
        perror("bind");
        close(tcp_socket);
        return -1;
    }

    ret =listen(tcp_socket,128);//监听套接字，等待客户端连接请求，第二个参数表示最大连接数

    if(ret==-1)
    {
        perror("listen");
        close(tcp_socket);
        return -1;
    }

    printf("listen %d...\n",port);

    return tcp_socket;

}
