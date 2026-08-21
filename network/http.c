#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../Tool/json.h"
#include "../Tool/send.h"
#include "http.h"


#define STATIC_ROOT "/home/xingxinliao/project/Http_Server_Project/Resource"

static int make_response(int status,
                         const char *reason,
                         const char *type,
                         const char *body,
                         size_t body_len,
                         char **response,
                         size_t *response_len)
{
    int header_len = snprintf(
        NULL,
        0,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: keep-alive\r\n\r\n",
        status,
        reason,
        type,
        body_len);

    if (header_len < 0) {
        return -1;
    }

    size_t response_size = (size_t)header_len + body_len + 1;
    *response = malloc(response_size);
    if (*response == NULL) {
        return -1;
    }

    snprintf(
        *response,
        response_size,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: keep-alive\r\n\r\n",
        status,
        reason,
        type,
        body_len);

    if (body_len != 0) {
        memcpy(*response + header_len, body, body_len);
    }

    *response_len = (size_t)header_len + body_len;
    return 0;
}



static int static_response(const char *path, char **response, size_t *response_len)
{
    if (strstr(path, "..") != NULL) {// 防止目录遍历攻击
        return make_response(
            403,
            "Forbidden",
            "text/plain",
            "Forbidden\n",
            10,
            response,
            response_len);
    }

    const char *name = path + strlen("/static/");
    if ((*name == '\0') || (strchr(name, '?') != NULL)) {// 如果请求的路径是 /static/ 或包含查询参数，返回 404
        return make_response(
            404,
            "Not Found",
            "text/plain",
            "Not Found\n",
            10,
            response,
            response_len);
    }

    char filepath[512];
    int path_len = snprintf(filepath, sizeof(filepath), "%s/%s", STATIC_ROOT, name);
    if ((path_len < 0) || ((size_t)path_len >= sizeof(filepath))) {
        return -1;
    }

    int fd = open(filepath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return make_response(
            404,
            "Not Found",
            "text/plain",
            "Not Found\n",
            10,
            response,
            response_len);
    }

    //声明一个 st 变量，用来保存文件的信息：类型、大小、权限等
    struct stat st;
    if ((fstat(fd, &st) == -1) || (!S_ISREG(st.st_mode)) || (st.st_size > 4 * 1024 * 1024)) {
        close(fd);
        return make_response(
            404,
            "Not Found",
            "text/plain",
            "Not Found\n",
            10,
            response,
            response_len);
    }

    size_t size = (size_t)st.st_size;
    char *body = malloc((size == 0) ? 1 : size);// 如果文件大小为 0，仍然分配 1 字节以避免 malloc(0) 的未定义行为
    if (body == NULL) {
        close(fd);
        return -1;
    }
    //把文件 fd 的全部内容读到 body 内存中；如果读失败，就释放内存并返回错误
    size_t got = 0;
    while (got < size) {
        ssize_t n = read(fd, body + got, size - got);
        if (n > 0) {
            got += (size_t)n;
            continue;
        }

        if ((n == -1) && (errno == EINTR)) {// 如果被信号中断，继续读取
            continue;
        }

        free(body);
        close(fd);
        return -1;
    }

    close(fd);

    int rc = make_response(
        200,
        "OK",
        "text/html; charset=UTF-8",
        body,
        size,
        response,
        response_len);

    free(body);
    return rc;
}


int build_http_response(const char *request,
                        size_t request_len,
                        int client_fd,
                        char **response,
                        size_t *response_len,
                        int *keep_alive
                         )
{
    (void)request_len;

    *response = NULL;
    *response_len = 0;

    // 默认设置为短连接 (HTTP/1.0 默认行为)
    // 如果你要完全遵守 HTTP/1.1，默认应该是 1，这里以基础实现为例
    *keep_alive = 0;

    // 解析请求头，判断是否有 Connection: keep-alive
    // 简单粗暴的做法：使用 strcasestr 忽略大小写查找
    // 注意：严谨的做法应该是逐行解析 HTTP Header
    const char *conn_header = strcasestr(request, "Connection:"); 
        
        if (conn_header) {
            if (strcasestr(conn_header, "keep-alive")) {
                *keep_alive = 1;
            } else if (strcasestr(conn_header, "close")) {
                *keep_alive = 0;
            }
        }


    char *copy = strdup(request);// 复制请求字符串，以便使用 strtok 分割
    if (copy == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    char *line_end = strstr(copy, "\r\n");
    if (line_end == NULL) {
        free(copy);
        return HTTP_RESPONSE_ERROR;// 如果请求行不完整，返回错误
    }
    *line_end = '\0';// 将请求行的结尾替换为字符串结束符，以便使用 strtok 分割

    // 【使用线程安全的 strtok_r】
    char *saveptr; // 用来保存字符串切割的内部状态
    char *method = strtok_r(copy, " ", &saveptr);
    char *path = strtok_r(NULL, " ", &saveptr);
    
    if ((method == NULL) || (path == NULL)) {
        free(copy);
        return HTTP_RESPONSE_ERROR;
    }

    const char *body = strstr(request, "\r\n\r\n");
    if (body == NULL) {
        body = "";
    } else {
        body = body + 4;
    }

    int rc;

    if ((strcmp(method, "POST") == 0) && (strcmp(path, "/upload") == 0)) {
        if ((parse_json_data(body) == 0) && (Send_Data_Quene() == 0)) {
            rc = make_response(
                200,
                "OK",
                "application/json",
                "{\"ok\":true}\n",
                12,
                response,
                response_len);
        } else {
            rc = make_response(
                400,
                "Bad Request",
                "application/json",
                "{\"ok\":false}\n",
                13,
                response,
                response_len);
        }
    } else if ((strcmp(method, "GET") == 0) && (strncmp(path, "/static/", 8) == 0)) {
        rc = static_response(path, response, response_len);
    } else if ((strcmp(method, "GET") == 0) && (strcmp(path, "/sensor/sensor.cgi") == 0)) {
        /* CGI currently owns the inherited socket and may block independently. */
        pid_t pid = fork();
        if (pid == 0) {
            char fd_string[16];
            snprintf(fd_string, sizeof(fd_string), "%d", client_fd);
            execl(
                "/home/xingxinliao/project/Http_Server_Project/CGI/cgi_query",
                "cgi_query",
                fd_string,
                NULL);
            _exit(127);// If execl fails, exit child process with error code
        }

        rc = (pid < 0) ? -1 : HTTP_RESPONSE_CGI_HANDOFF;
    } else {
        rc = make_response(
            404,
            "Not Found",
            "text/plain",
            "Not Found\n",
            10,
            response,
            response_len);
    }

    free(copy);

    if (rc == 0) {
        return HTTP_RESPONSE_READY;
    }

    return rc;
}
