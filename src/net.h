/*
 * net.h - TCP helpers shared by the L2 connection and the L2 Terminal
 */
#ifndef SGIL2_NET_H
#define SGIL2_NET_H

#include <stddef.h>

/*
 * Connect to host:port within timeout_ms. Returns a non-blocking socket,
 * or -1 with a message in err.
 */
int  net_connect(const char *host, int port, int timeout_ms, char *err, size_t errlen);
/* write all of data to a non-blocking socket; 0 on success, -1 on error */
int  net_write_all(int fd, const char *data, size_t len);
/* wait up to timeout_ms for fd to become readable: 1 yes, 0 timeout, -1 error */
int  net_wait_readable(int fd, long timeout_ms);
/* milliseconds since some fixed point */
long net_now_ms(void);
/* the local IPv4 address of a connected socket (network order bytes) */
void net_local_addr(int fd, unsigned char ip[4]);

#endif
