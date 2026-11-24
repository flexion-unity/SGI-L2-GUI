/*
 * telnet.h - minimal telnet client for the SGI L2 controller
 *
 * One connection at a time. All option negotiation is refused
 * (DO -> WONT, WILL -> DONT), which keeps the L2 in plain NVT line mode.
 * A command is complete when the configured shell prompt is seen at the
 * end of the received data.
 *
 * The API is split into begin / pump / take so it can be driven either
 * synchronously (tn_exec, CLI mode) or from an event loop (XtAppAddInput).
 */
#ifndef SGIL2_TELNET_H
#define SGIL2_TELNET_H

#include <stddef.h>

#define TN_AGAIN    0   /* need more data */
#define TN_DONE     1   /* prompt seen, response complete */
#define TN_EOF     -1   /* connection closed / error */
#define TN_TIMEOUT -2

int         tn_connect(const char *host, int port, int timeout_ms,
                       char *err, size_t errlen);
void        tn_close(void);
int         tn_connected(void);
int         tn_fd(void);

void        tn_set_prompt(const char *prompt);
const char *tn_get_prompt(void);
void        tn_set_eol(const char *eol);

/* discard pending input, send cmd + eol. returns 0 on success */
int         tn_begin(const char *cmd);
/* non-blocking read of what is available; TN_AGAIN / TN_DONE / TN_EOF */
int         tn_pump(void);
/* raw data received since tn_begin (IAC stripped) */
const char *tn_raw(void);
/* malloc'd cleaned response of cmd: echo, prompt and junk removed */
char       *tn_take_response(const char *cmd);

/* block until TN_DONE / TN_EOF / TN_TIMEOUT */
int         tn_wait(int timeout_ms);
/* synchronous helper: *out receives malloc'd response (may be NULL) */
int         tn_exec(const char *cmd, int timeout_ms, char **out);

/*
 * After connecting: wake up the L2 with an empty line and wait for the
 * prompt. If no prompt is configured, learn it from the last line
 * received. returns 0 on success.
 */
int         tn_sync_prompt(int timeout_ms, char *err, size_t errlen);

#endif
