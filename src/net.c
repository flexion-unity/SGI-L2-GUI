/*
 * net.c - TCP helpers shared by the L2 connection and the L2 Terminal
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef __sgi
#include <bstring.h>    /* FD_ZERO uses bzero on IRIX */
#endif

#include "net.h"
#include "util.h"

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffffU
#endif

#ifdef __sgi
typedef int net_socklen_t;
#else
typedef socklen_t net_socklen_t;
#endif

long net_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

int net_connect(const char *host, int port, int timeout_ms, char *err, size_t errlen)
{
    struct sockaddr_in sa;
    struct hostent *he;
    fd_set wfds;
    struct timeval tv;
    int fd, flags, rc, soerr;
    net_socklen_t slen;

    if (!host || !*host) {
        s_copy(err, "No L2 host configured", errlen);
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = inet_addr(host);
    if (sa.sin_addr.s_addr == (unsigned)INADDR_NONE) {
        he = gethostbyname(host);
        if (!he || he->h_addrtype != AF_INET) {
            s_copy(err, "Unknown host: ", errlen);
            s_cat(err, host, errlen);
            return -1;
        }
        memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof(sa.sin_addr));
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        s_copy(err, strerror(errno), errlen);
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno != EINPROGRESS) {
        s_copy(err, strerror(errno), errlen);
        close(fd);
        return -1;
    }
    if (rc < 0) {
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        rc = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (rc <= 0) {
            s_copy(err, rc == 0 ? "Connection timed out" : strerror(errno), errlen);
            close(fd);
            return -1;
        }
        soerr = 0;
        slen = (net_socklen_t)sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soerr, &slen);
        if (soerr != 0) {
            s_copy(err, strerror(soerr), errlen);
            close(fd);
            return -1;
        }
    }
    return fd;
}

int net_write_all(int fd, const char *data, size_t len)
{
    fd_set wfds;
    struct timeval tv;
    ssize_t n;

    if (fd < 0) return -1;
    while (len > 0) {
        n = write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                FD_ZERO(&wfds);
                FD_SET(fd, &wfds);
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) return -1;
                continue;
            }
            return -1;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

int net_wait_readable(int fd, long timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    int rc;

    if (timeout_ms < 0) timeout_ms = 0;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    rc = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (rc < 0) return errno == EINTR ? 0 : -1;
    return rc > 0;
}

void net_local_addr(int fd, unsigned char ip[4])
{
    struct sockaddr_in sa;
    net_socklen_t slen = (net_socklen_t)sizeof(sa);
    memset(ip, 0, 4);
    memset(&sa, 0, sizeof(sa));
    if (getsockname(fd, (struct sockaddr *)&sa, &slen) == 0 && sa.sin_family == AF_INET)
        memcpy(ip, &sa.sin_addr.s_addr, 4);     /* already network order */
}
