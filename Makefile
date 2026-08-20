CC = gcc
CFLAGS = -Wall -Wextra -O2

# 主程序目标和 CGI 目标
MAIN_TARGET = epoll_server_project
CGI_TARGET = CGI/cgi_query

# 主程序源码（移除 CGI/cgi_query.c）
MAIN_SRCS = main.c network/socket.c epoll_server/epoll_server.c network/http.c \
            Tool/json.c Tool/send.c /home/xingxinliao/lab/cJSON/cJSON.c \
            Tool/no_find.c Tool/process_create.c epoll_server/Thread_pool.c

# CGI 源码
CGI_SRCS = CGI/cgi_query.c

MAIN_OBJS = $(MAIN_SRCS:.c=.o)
CGI_OBJS = $(CGI_SRCS:.c=.o)

LDLIBS = -lm

.PHONY: all clean

all: $(MAIN_TARGET) $(CGI_TARGET)

$(MAIN_TARGET): $(MAIN_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJS) $(LDLIBS)

$(CGI_TARGET): $(CGI_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CGI_OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(MAIN_TARGET) $(CGI_TARGET) $(MAIN_OBJS) $(CGI_OBJS)

