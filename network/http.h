#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <sys/types.h>

/* Return values for build_http_response(). */
#define HTTP_RESPONSE_READY 0
#define HTTP_RESPONSE_CGI_HANDOFF 1
#define HTTP_RESPONSE_ERROR -1
#define MAX_REQUEST_SIZE (64 * 1024)

/*
 * Builds one complete response owned by the caller.  The function never
 * writes to client_fd; that is deliberately left to the epoll Reactor.
 */
int build_http_response(const char *request, size_t request_len, int client_fd,
                        char **response, size_t *response_len, int *keep_alive);
ssize_t complete_http_request(const char *buf, size_t len);                       

#endif
