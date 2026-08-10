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
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
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
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
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
    if (strstr(path, "..") != NULL) {
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
    if ((*name == '\0') || (strchr(name, '?') != NULL)) {
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
    char *body = malloc((size == 0) ? 1 : size);
    if (body == NULL) {
        close(fd);
        return -1;
    }

    size_t got = 0;
    while (got < size) {
        ssize_t n = read(fd, body + got, size - got);
        if (n > 0) {
            got += (size_t)n;
            continue;
        }

        if ((n == -1) && (errno == EINTR)) {
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
                        size_t *response_len)
{
    (void)request_len;

    *response = NULL;
    *response_len = 0;

    char *copy = strdup(request);
    if (copy == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    char *line_end = strstr(copy, "\r\n");
    if (line_end == NULL) {
        free(copy);
        return HTTP_RESPONSE_ERROR;
    }
    *line_end = '\0';

    char *method = strtok(copy, " ");
    char *path = strtok(NULL, " ");
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
            _exit(127);
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
