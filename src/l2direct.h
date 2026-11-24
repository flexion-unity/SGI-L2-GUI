/*
 * l2direct.h - talk to the L2 directly, as SGI's l2cmd does (SCapi.c)
 *
 * One Router connection to the L2's command port (TCP 8000), over which
 * text commands go to the L2's command task as IRouter requests and come
 * back with their output and status: no shell prompt to match, no connect
 * delay, no idle disconnect. An L2 password is supported.
 *
 * The API is split into begin / pump / take so it can be driven either
 * synchronously (l2d_exec, CLI mode) or from an event loop (XtAppAddInput).
 *
 * Ported from SGI-L2-WUI backend/functions/l2-client.js.
 */
#ifndef SGIL2_L2DIRECT_H
#define SGIL2_L2DIRECT_H

#include <stddef.h>

#define L2D_AGAIN    0      /* need more data */
#define L2D_DONE     1      /* the response is complete */
#define L2D_EOF     -1      /* connection closed / error */
#define L2D_TIMEOUT -2

#define L2D_DEFAULT_PORT  8000
#define L2D_BUSY_RETRIES  5         /* the L2 runs one command at a time, for all clients */
#define L2D_BUSY_WAIT_MS  1000
#define L2D_PART_MAX      900       /* longest command part that fits a request frame */

/* blocking connect + handshake; 0 on success, -1 with a message in err */
int   l2d_connect(const char *host, int port, const char *password,
                  int timeout_ms, char *err, size_t errlen);
void  l2d_close(void);
int   l2d_connected(void);
int   l2d_fd(void);
/* why the connection was closed by the L2 / lost */
const char *l2d_error(void);

/*
 * Send one command part (no ';'). 0: sent, wait for L2D_DONE from
 * l2d_pump; 1: done, no response comes (reboot); -1: send failed (closed);
 * -2: command too long.
 */
int   l2d_begin(const char *part);
/* non-blocking read of what is available; L2D_AGAIN / L2D_DONE / L2D_EOF */
int   l2d_pump(void);
/* malloc'd output of the answered command, *status its L2 status */
char *l2d_take_response(int *status);
/* is this the L2 telling us it is busy with another command? */
int   l2d_is_busy(const char *resp, int status);

/*
 * The next ';'-separated part of a command line, trimmed, into buf.
 * Returns the position after it, or NULL when no parts are left.
 */
const char *l2d_next_part(const char *p, char *buf, size_t size);

/* synchronous: all parts in turn, busy retries; *out malloc'd output.
   returns L2D_DONE / L2D_EOF / L2D_TIMEOUT */
int   l2d_exec(const char *cmd, int timeout_ms, char **out);

#endif
