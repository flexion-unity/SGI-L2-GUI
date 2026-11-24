/*
 * telnet.c - minimal telnet client for the SGI L2 controller
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

#include "telnet.h"
#include "util.h"

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffffU
#endif

#ifdef __sgi
typedef int tn_socklen_t;
#else
typedef socklen_t tn_socklen_t;
#endif

/* telnet protocol bytes */
#define T_SE    240
#define T_SB    250
#define T_WILL  251
#define T_WONT  252
#define T_DO    253
#define T_DONT  254
#define T_IAC   255

enum { ST_DATA, ST_IAC, ST_OPT, ST_SB, ST_SB_IAC };

static int  tn_sock = -1;
static char tn_prompt_str[64];
static char tn_eol_str[4] = "\n";
static DBuf tn_buf;
static int  tn_state = ST_DATA;
static int  tn_verb = 0;
static int  tn_inflight = 0;    /* waiting for a prompt (command or banner) */
/* remember what we already refused, so we never loop on negotiation */
static unsigned char tn_refused_do[256];
static unsigned char tn_refused_will[256];

static long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

static int write_all(const char *data, size_t len)
{
    fd_set wfds;
    struct timeval tv;
    ssize_t n;

    while (len > 0) {
        n = write(tn_sock, data, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                FD_ZERO(&wfds);
                FD_SET(tn_sock, &wfds);
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                if (select(tn_sock + 1, NULL, &wfds, NULL, &tv) <= 0) return -1;
                continue;
            }
            return -1;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static void send_opt(int verb, int opt)
{
    unsigned char b[3];
    b[0] = T_IAC;
    b[1] = (unsigned char)verb;
    b[2] = (unsigned char)opt;
    write_all((const char *)b, 3);
}

int tn_connect(const char *host, int port, int timeout_ms, char *err, size_t errlen)
{
    struct sockaddr_in sa;
    struct hostent *he;
    fd_set wfds;
    struct timeval tv;
    int flags, rc, soerr;
    tn_socklen_t slen;

    tn_close();
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

    tn_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tn_sock < 0) {
        s_copy(err, strerror(errno), errlen);
        return -1;
    }
    flags = fcntl(tn_sock, F_GETFL, 0);
    fcntl(tn_sock, F_SETFL, flags | O_NONBLOCK);

    rc = connect(tn_sock, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno != EINPROGRESS) {
        s_copy(err, strerror(errno), errlen);
        tn_close();
        return -1;
    }
    if (rc < 0) {
        FD_ZERO(&wfds);
        FD_SET(tn_sock, &wfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        rc = select(tn_sock + 1, NULL, &wfds, NULL, &tv);
        if (rc <= 0) {
            s_copy(err, rc == 0 ? "Connection timed out" : strerror(errno), errlen);
            tn_close();
            return -1;
        }
        soerr = 0;
        slen = (tn_socklen_t)sizeof(soerr);
        getsockopt(tn_sock, SOL_SOCKET, SO_ERROR, (char *)&soerr, &slen);
        if (soerr != 0) {
            s_copy(err, strerror(soerr), errlen);
            tn_close();
            return -1;
        }
    }
    if (!tn_buf.data) dbuf_init(&tn_buf);
    dbuf_clear(&tn_buf);
    tn_state = ST_DATA;
    tn_inflight = 1;    /* the banner ends with the first prompt */
    memset(tn_refused_do, 0, sizeof(tn_refused_do));
    memset(tn_refused_will, 0, sizeof(tn_refused_will));
    return 0;
}

void tn_close(void)
{
    if (tn_sock >= 0) close(tn_sock);
    tn_sock = -1;
}

int tn_connected(void)
{
    return tn_sock >= 0;
}

int tn_fd(void)
{
    return tn_sock;
}

void tn_set_prompt(const char *prompt)
{
    s_copy(tn_prompt_str, prompt ? prompt : "", sizeof(tn_prompt_str));
}

const char *tn_get_prompt(void)
{
    return tn_prompt_str;
}

void tn_set_eol(const char *eol)
{
    s_copy(tn_eol_str, eol ? eol : "\n", sizeof(tn_eol_str));
}

/* feed raw socket bytes through the telnet state machine */
static void feed(const unsigned char *p, size_t n)
{
    size_t i;
    unsigned char c;
    for (i = 0; i < n; i++) {
        c = p[i];
        switch (tn_state) {
        case ST_DATA:
            if (c == T_IAC) tn_state = ST_IAC;
            else if (c != 0) dbuf_appendc(&tn_buf, (char)c);
            break;
        case ST_IAC:
            if (c == T_IAC) {
                tn_state = ST_DATA;     /* escaped 255, not printable anyway */
            } else if (c == T_WILL || c == T_WONT || c == T_DO || c == T_DONT) {
                tn_verb = c;
                tn_state = ST_OPT;
            } else if (c == T_SB) {
                tn_state = ST_SB;
            } else {
                tn_state = ST_DATA;     /* NOP, GA, ... */
            }
            break;
        case ST_OPT:
            if (tn_verb == T_DO && !tn_refused_do[c]) {
                tn_refused_do[c] = 1;
                send_opt(T_WONT, c);
            } else if (tn_verb == T_WILL && !tn_refused_will[c]) {
                tn_refused_will[c] = 1;
                send_opt(T_DONT, c);
            }
            tn_state = ST_DATA;
            break;
        case ST_SB:
            if (c == T_IAC) tn_state = ST_SB_IAC;
            break;
        case ST_SB_IAC:
            tn_state = (c == T_SE) ? ST_DATA : ST_SB;
            break;
        }
    }
}

/* does the received data end with the prompt? */
static int prompt_seen(void)
{
    size_t pl = strlen(tn_prompt_str), l = tn_buf.len;
    const char *d = tn_buf.data;
    if (pl == 0) return 0;
    while (l > 0 && (d[l - 1] == ' ' || d[l - 1] == '\t' || d[l - 1] == '\r' ||
                     d[l - 1] == '\n'))
        l--;
    if (l < pl) return 0;
    return memcmp(d + l - pl, tn_prompt_str, pl) == 0;
}

/* read everything currently available; returns bytes read or -1 on EOF */
static int read_available(void)
{
    unsigned char buf[4096];
    ssize_t n;
    int total = 0;
    for (;;) {
        n = read(tn_sock, buf, sizeof(buf));
        if (n > 0) {
            feed(buf, (size_t)n);
            total += (int)n;
            continue;
        }
        if (n == 0) return -1;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return total;
        return -1;
    }
}

int tn_begin(const char *cmd)
{
    if (tn_sock < 0) return -1;
    /* drop stray output (extra prompts, async messages) */
    if (read_available() < 0) {
        tn_close();
        return -1;
    }
    dbuf_clear(&tn_buf);
    tn_inflight = 1;
    if (write_all(cmd, strlen(cmd)) < 0 ||
        write_all(tn_eol_str, strlen(tn_eol_str)) < 0) {
        tn_close();
        return -1;
    }
    return 0;
}

int tn_pump(void)
{
    int eof;
    if (tn_sock < 0) return TN_EOF;
    eof = read_available() < 0;
    if (eof) tn_close();
    if (!tn_inflight) {
        /* idle: a stale prompt in the buffer must not count as a reply */
        return eof ? TN_EOF : TN_AGAIN;
    }
    if (prompt_seen()) {
        tn_inflight = 0;
        return TN_DONE;
    }
    return eof ? TN_EOF : TN_AGAIN;
}

const char *tn_raw(void)
{
    return tn_buf.data ? tn_buf.data : "";
}

char *tn_take_response(const char *cmd)
{
    DBuf out;
    char *copy, *p, *nl, *line, *trimmed, *res;
    char cmdt[256];
    int first = 1;
    size_t pl = strlen(tn_prompt_str);

    s_copy(cmdt, cmd ? cmd : "", sizeof(cmdt));
    copy = s_dup(tn_raw());
    s_strip_nonprint(copy);
    s_rtrim(copy);
    /* strip trailing prompt */
    if (pl > 0 && s_endswith(copy, tn_prompt_str)) {
        copy[strlen(copy) - pl] = '\0';
    }

    dbuf_init(&out);
    p = copy;
    while (p && *p) {
        nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        line = p;
        /* drop CRs */
        {
            char *r = line, *w = line;
            for (; *r; r++) if (*r != '\r') *w++ = *r;
            *w = '\0';
        }
        trimmed = s_trim(line);
        /* echo of the command (possibly prefixed by the prompt) */
        if (first && *cmdt && s_endswith(trimmed, s_trim(cmdt))) {
            first = 0;
        } else {
            if (first && *trimmed == '\0') {
                /* skip leading blank lines */
            } else {
                first = 0;
                s_rtrim(line);
                dbuf_appends(&out, line);
                dbuf_appendc(&out, '\n');
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    free(copy);
    /* trailing blank lines */
    while (out.len > 0 && (out.data[out.len - 1] == '\n' || out.data[out.len - 1] == ' '))
        out.data[--out.len] = '\0';
    res = s_dup(out.data);
    dbuf_free(&out);
    return res;
}

int tn_wait(int timeout_ms)
{
    long deadline = now_ms() + timeout_ms, left;
    fd_set rfds;
    struct timeval tv;
    int rc;

    for (;;) {
        rc = tn_pump();
        if (rc != TN_AGAIN) return rc;
        left = deadline - now_ms();
        if (left <= 0) return TN_TIMEOUT;
        FD_ZERO(&rfds);
        FD_SET(tn_sock, &rfds);
        tv.tv_sec = left / 1000;
        tv.tv_usec = (left % 1000) * 1000;
        rc = select(tn_sock + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0 && errno != EINTR) return TN_EOF;
    }
}

int tn_exec(const char *cmd, int timeout_ms, char **out)
{
    int rc;
    if (out) *out = NULL;
    if (tn_begin(cmd) < 0) return TN_EOF;
    rc = tn_wait(timeout_ms);
    if (rc == TN_DONE && out) *out = tn_take_response(cmd);
    return rc;
}

/* collect input until quiet for quiet_ms or total_ms passed */
static void collect_quiet(int quiet_ms, int total_ms)
{
    long start = now_ms(), last = now_ms();
    fd_set rfds;
    struct timeval tv;
    int n;

    while (now_ms() - start < total_ms && now_ms() - last < quiet_ms) {
        FD_ZERO(&rfds);
        FD_SET(tn_sock, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        if (select(tn_sock + 1, &rfds, NULL, NULL, &tv) > 0) {
            n = read_available();
            if (n < 0) {
                tn_close();
                return;
            }
            if (n > 0) last = now_ms();
        }
    }
}

int tn_sync_prompt(int timeout_ms, char *err, size_t errlen)
{
    char *copy, *last, *p;
    int rc;

    if (tn_sock < 0) {
        s_copy(err, "Not connected", errlen);
        return -1;
    }
    if (tn_prompt_str[0]) {
        /* known prompt: banner may already end with it */
        rc = tn_wait(1500);
        if (rc == TN_DONE) return 0;
        if (rc == TN_EOF) {
            s_copy(err, "Connection closed by L2", errlen);
            return -1;
        }
        if (tn_begin("") < 0) {
            s_copy(err, "Connection closed by L2", errlen);
            return -1;
        }
        rc = tn_wait(timeout_ms);
        if (rc == TN_DONE) return 0;
        if (rc == TN_EOF) s_copy(err, "Connection closed by L2", errlen);
        else {
            s_copy(err, "Prompt \"", errlen);
            s_cat(err, tn_prompt_str, errlen);
            s_cat(err, "\" not seen - check the prompt setting", errlen);
        }
        return -1;
    }

    /* auto detect: let the banner arrive, press enter, take last line */
    collect_quiet(700, 3000);
    if (tn_sock < 0 || tn_begin("") < 0) {
        s_copy(err, "Connection closed by L2", errlen);
        return -1;
    }
    collect_quiet(700, timeout_ms);
    if (tn_sock < 0) {
        s_copy(err, "Connection closed by L2", errlen);
        return -1;
    }
    copy = s_dup(tn_raw());
    s_strip_nonprint(copy);
    s_rtrim(copy);
    last = copy;
    for (p = copy; *p; p++)
        if (*p == '\n' || *p == '\r') last = p + 1;
    last = s_trim(last);
    if (*last == '\0') {
        free(copy);
        s_copy(err, "Could not detect the L2 prompt - set it in Settings", errlen);
        return -1;
    }
    tn_set_prompt(last);
    tn_inflight = 0;
    free(copy);
    return 0;
}
