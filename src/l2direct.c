/*
 * l2direct.c - talk to the L2 directly over its Router protocol (see l2direct.h)
 *
 * Connecting: the server speaks first, with ACCEPT (or PASSWORD_REQ, or
 * DENIED), opcode 2 (its process ID); we answer with CLIENT_ID (opcode: our
 * task, which the L2 makes our event mask), then the server's CONFIG_INIT is
 * echoed as CONFIG_REQ and comes back as CONFIG_RSP. After that an L3 client
 * and the L2 exchange only events, each carrying an IRouter packet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "l2direct.h"
#include "telnet.h"     /* TN_* return codes */
#include "router.h"
#include "irouter.h"
#include "net.h"
#include "util.h"

#define TASK        4           /* our event mask: the L2's configuration events (as l2api) */
#define PROCNAME    "sgil2gui"

enum { DS_CLOSED, DS_ACCEPT, DS_CONFIG, DS_READY };

static int          d_sock = -1;
static int          d_state = DS_CLOSED;
static RtParser     d_parser;
static int          d_parser_init;
static char         d_err[256];
static unsigned int d_seq;
static int          d_waiting;      /* a request is out, its seq in d_wait_seq */
static unsigned int d_wait_seq;
static int          d_have;         /* its response has arrived */
static int          d_status;
static DBuf         d_resp;

/* the Router argument before each IRouter frame: the sender's 16-byte
   in-memory prefix (IRouter2Router.c; byte order 1: the frame is big-endian) */
static const unsigned char ir_prefix[16] = {
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int send_packet(unsigned int opcode, int clas, const RtArg *args, int nargs)
{
    DBuf b;
    int rc;
    dbuf_init(&b);
    rt_pack(&b, opcode, clas, args, nargs);
    rc = net_write_all(d_sock, b.data, b.len);
    dbuf_free(&b);
    return rc;
}

static void set_err(const char *msg)
{
    s_copy(d_err, msg, sizeof(d_err));
}

void l2d_close(void)
{
    if (d_sock >= 0) close(d_sock);
    d_sock = -1;
    d_state = DS_CLOSED;
    d_waiting = 0;
    d_have = 0;
    if (d_parser_init) rt_parser_reset(&d_parser);
}

int l2d_connected(void)
{
    return d_sock >= 0 && d_state == DS_READY;
}

int l2d_fd(void)
{
    return d_sock;
}

const char *l2d_error(void)
{
    return d_err[0] ? d_err : "connection lost";
}

/* a byte argument from a received one (a value as its 4 bytes) */
static void echo_arg(const RtPacket *pkt, RtArg *a, unsigned char tmp[4])
{
    if (pkt->nargs < 1) {
        a->isValue = 0;
        a->data = tmp;
        a->len = 0;
        return;
    }
    *a = pkt->args[0];
    if (a->isValue) {
        rt_put32(tmp, a->value);
        a->isValue = 0;
        a->data = tmp;
        a->len = 4;
    }
}

static long sign32(unsigned long v)
{
    v &= 0xffffffffUL;
    if (v & 0x80000000UL) return -(long)((~v + 1UL) & 0xffffffffUL);
    return (long)v;
}

/* an IRouter packet in a Router event: maybe our command's response */
static void ir_event(const RtPacket *pkt)
{
    const unsigned char *frames[RT_MAX_ARGS];
    size_t lens[RT_MAX_ARGS];
    int i, n = 0;
    IrPacket ip;
    char err[128], ebuf[64];
    const char *text;
    unsigned int want;

    if (pkt->nargs < 2 || pkt->args[0].isValue || pkt->args[0].len < 3 ||
        memcmp(pkt->args[0].data, "IR\0", 3) != 0)
        return;
    for (i = 1; i < pkt->nargs; i++) {
        if (pkt->args[i].isValue || pkt->args[i].len < 16) continue;
        frames[n] = pkt->args[i].data + 16;
        lens[n] = pkt->args[i].len - 16;
        n++;
    }
    if (ir_decode(frames, lens, n, &ip, err, sizeof(err)) < 0) {
        fprintf(stderr, "sgil2gui: bad packet from the L2: %s\n", err);
        return;
    }
    if (ip.clas != IR_CLAS_RSP || !d_waiting) {
        ir_packet_free(&ip);    /* (events, e.g. EV_L2_CONFIG: not used) */
        return;
    }
    /* revision 0 packets carry only 8 bits of sequence number */
    want = ip.rev == 0 ? (d_wait_seq & 0xff) : d_wait_seq;
    if (ip.seqnum != want) {
        ir_packet_free(&ip);
        return;
    }
    dbuf_clear(&d_resp);
    if (ip.err != 0) {
        d_status = -600 - ip.err;
        dbuf_appends(&d_resp, "Command response ERROR: ");
        dbuf_appends(&d_resp, ir_error_text(ip.err, ebuf, sizeof(ebuf)));
        dbuf_appendc(&d_resp, '\n');
    } else {
        d_status = 0;
        if (ip.argc > 0 && ip.args[0].type == IR_ARG_VALUE)
            d_status = (int)sign32(ip.args[0].value);
        for (i = 0; i < ip.argc; i++) {
            if (ip.args[i].type != IR_ARG_STRING) continue;
            text = (const char *)ip.args[i].data;     /* (NUL terminated) */
            dbuf_appends(&d_resp, text);
            if (!*text || text[strlen(text) - 1] != '\n') dbuf_appendc(&d_resp, '\n');
        }
    }
    ir_packet_free(&ip);
    d_waiting = 0;
    d_have = 1;
}

/* a packet after the handshake started; -1 = connection is to be closed */
static int handle_packet(const RtPacket *pkt, const char *password, int *pwsent)
{
    RtArg a[2];
    unsigned char tmp[8], ipaddr[4];
    DBuf id;
    const char *user;
    int rc;

    if (d_state == DS_ACCEPT) {
        if (pkt->opcode != RT_SERVER_PROCESS_ID) {
            sprintf(d_err, "Received an incorrect process ID from the L2 (%u)", pkt->opcode);
            return -1;
        }
        switch (pkt->clas) {
        case RC_ACCEPT:
            memset(tmp, 0, sizeof(tmp));
            rt_put32(tmp, RT_RU_CONFIGURE);
            user = getenv("LOGNAME");
            if (!user || !*user) user = getenv("USER");
            if (!user || !*user) user = "l2gui";
            net_local_addr(d_sock, ipaddr);
            dbuf_init(&id);
            rt_pack_id(&id, PROCNAME, user, ipaddr);
            a[0].isValue = 0; a[0].data = tmp; a[0].len = 8;
            a[1].isValue = 0; a[1].data = (const unsigned char *)id.data; a[1].len = id.len;
            rc = send_packet(TASK, RC_CLIENT_ID, a, 2);
            dbuf_free(&id);
            if (rc < 0) {
                set_err("write to the L2 failed");
                return -1;
            }
            d_state = DS_CONFIG;
            return 0;
        case RC_PASSWORD_REQ:
            /* the server asks again after a wrong one, then denies us */
            if ((*pwsent)++ > 0) {
                set_err("the L2 refused the password");
                return -1;
            }
            a[0].isValue = 0;
            a[0].data = (const unsigned char *)(password ? password : "");
            a[0].len = strlen(password ? password : "") + 1;
            if (send_packet(TASK, RC_PASSWORD, a, 1) < 0) {
                set_err("write to the L2 failed");
                return -1;
            }
            return 0;
        case RC_DENIED:
            set_err(*pwsent ? "the L2 refused the password" : "Connection was denied by the L2");
            return -1;
        default:
            sprintf(d_err, "unexpected packet class 0x%x while connecting", pkt->clas);
            return -1;
        }
    }

    switch (pkt->clas) {
    case RC_CONFIG_INIT:        /* (echoed verbatim, as SGI's clients do) */
        echo_arg(pkt, &a[0], tmp);
        if (send_packet(0, RC_CONFIG_REQ, a, 1) < 0) {
            set_err("write to the L2 failed");
            return -1;
        }
        return 0;
    case RC_CONFIG_REQ:
        echo_arg(pkt, &a[0], tmp);
        if (send_packet(0, RC_CONFIG_RSP, a, 1) < 0) {
            set_err("write to the L2 failed");
            return -1;
        }
        d_state = DS_READY;
        return 0;
    case RC_CONFIG_RSP:
        d_state = DS_READY;
        return 0;
    case RC_EVENT:
        if (d_state == DS_READY) ir_event(pkt);
        return 0;
    case RC_DISCONNECT_REQ:
        send_packet(0, RC_DISCONNECT_RSP, NULL, 0);
        set_err("the L2 closed the connection");
        return -1;
    case RC_PING:
    case RC_REQUEST:            /* (not used between an L2 and an L3 client) */
    case RC_REL_REQUEST:
    case RC_RESPONSE:
        return 0;
    default:
        sprintf(d_err, "invalid packet class 0x%x from the L2", pkt->clas);
        return -1;
    }
}

/* read what is available and handle whole packets; -1 = closed */
static int read_and_handle(const char *password, int *pwsent)
{
    unsigned char buf[4096];
    static RtPacket pkt;        /* (large: RT_MAX_ARGS arguments) */
    ssize_t n;
    int rc, closed = 0;

    for (;;) {
        n = read(d_sock, buf, sizeof(buf));
        if (n > 0) {
            rt_feed(&d_parser, buf, (size_t)n);
            continue;
        }
        if (n == 0) {
            set_err("connection lost");
            closed = 1;
            break;
        }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            set_err(strerror(errno));
            closed = 1;
        }
        break;
    }
    /* (what arrived before a close is still handled) */
    while ((rc = rt_next(&d_parser, &pkt, d_err, sizeof(d_err))) > 0) {
        if (handle_packet(&pkt, password, pwsent) < 0) return -1;
    }
    if (rc < 0) return -1;
    return closed ? -1 : 0;
}

int l2d_connect(const char *host, int port, const char *password,
                int timeout_ms, char *err, size_t errlen)
{
    long deadline;
    int pwsent = 0, rc;

    l2d_close();
    d_err[0] = '\0';
    if (!d_parser_init) {
        rt_parser_init(&d_parser);
        dbuf_init(&d_resp);
        d_parser_init = 1;
    }
    rt_parser_reset(&d_parser);
    d_sock = net_connect(host, port > 0 ? port : L2D_DEFAULT_PORT, timeout_ms, err, errlen);
    if (d_sock < 0) return -1;
    d_state = DS_ACCEPT;
    deadline = net_now_ms() + (timeout_ms > 0 ? timeout_ms : 10000);
    while (d_state != DS_READY) {
        rc = net_wait_readable(d_sock, deadline - net_now_ms());
        if (rc == 0 && net_now_ms() >= deadline) {
            s_copy(err, "No handshake from the L2 - is this its command port (8000)?", errlen);
            l2d_close();
            return -1;
        }
        if (rc < 0 || read_and_handle(password, &pwsent) < 0) {
            s_copy(err, l2d_error(), errlen);
            l2d_close();
            return -1;
        }
    }
    return 0;
}

int l2d_begin(const char *part)
{
    IrArgIn args[2];
    RtArg ra[2];
    DBuf frame, ev;
    unsigned int seq;
    int rc;

    if (!l2d_connected()) return -1;
    if (strlen(part) > L2D_PART_MAX) return -2;
    seq = d_seq;
    d_seq = (d_seq + 1) & 0xffff;

    args[0].type = IR_ARG_VALUE;
    args[0].value = 0;
    args[0].data = NULL;
    args[0].len = 0;
    args[1].type = IR_ARG_STRING;
    args[1].value = 0;
    args[1].data = (const unsigned char *)part;
    args[1].len = strlen(part) + 1;
    dbuf_init(&frame);
    dbuf_append(&frame, (const char *)ir_prefix, sizeof(ir_prefix));
    if (ir_encode(&frame, IR_CLAS_REQ, seq, IR_ADDR_L2_CMD, IR_ADDR_L3, 0, 0, args, 2) < 0) {
        dbuf_free(&frame);
        return -2;
    }
    ra[0].isValue = 0;
    ra[0].data = (const unsigned char *)"IR";       /* "IR\0" */
    ra[0].len = 3;
    ra[1].isValue = 0;
    ra[1].data = (const unsigned char *)frame.data;
    ra[1].len = frame.len;
    dbuf_init(&ev);
    rt_pack(&ev, 0, RC_EVENT, ra, 2);
    dbuf_free(&frame);

    d_have = 0;
    dbuf_clear(&d_resp);
    d_status = 0;
    d_waiting = 1;
    d_wait_seq = seq;
    rc = net_write_all(d_sock, ev.data, ev.len);
    dbuf_free(&ev);
    if (rc < 0) {
        set_err("write to the L2 failed");
        l2d_close();
        return -1;
    }
    if (strstr(part, "reboot")) {      /* (no response comes) */
        d_waiting = 0;
        d_have = 1;
        return 1;
    }
    return 0;
}

int l2d_pump(void)
{
    int dummy = 1;
    if (d_sock < 0) return TN_EOF;
    if (read_and_handle(NULL, &dummy) < 0) {
        l2d_close();
        return d_have ? TN_DONE : TN_EOF;
    }
    return d_have ? TN_DONE : TN_AGAIN;
}

char *l2d_take_response(int *status)
{
    char *s, *r, *w;
    size_t l;

    s = s_dup(d_resp.data ? d_resp.data : "");
    if (status) *status = d_status;
    d_have = 0;
    s_strip_nonprint(s);
    for (r = w = s; *r; r++)
        if (*r != '\r') *w++ = *r;
    *w = '\0';
    l = strlen(s);
    while (l > 0 && (s[l - 1] == '\n' || s[l - 1] == ' ')) s[--l] = '\0';
    return s;
}

int l2d_is_busy(const char *resp, int status)
{
    return status != 0 && resp && strstr(resp, "command library is busy") != NULL;
}

const char *l2d_next_part(const char *p, char *buf, size_t size)
{
    const char *e;
    char *t;
    size_t n;

    while (p && *p) {
        e = strchr(p, ';');
        n = e ? (size_t)(e - p) : strlen(p);
        if (n >= size) n = size - 1;
        memcpy(buf, p, n);
        buf[n] = '\0';
        p = e ? e + 1 : p + strlen(p);
        t = s_trim(buf);
        if (*t) {
            if (t != buf) memmove(buf, t, strlen(t) + 1);
            return p;
        }
    }
    return NULL;
}

/* wait for the response to the request just sent */
static int wait_response(int timeout_ms)
{
    long deadline = net_now_ms() + timeout_ms;
    int rc;
    for (;;) {
        rc = l2d_pump();
        if (rc != TN_AGAIN) return rc;
        if (net_now_ms() >= deadline) return TN_TIMEOUT;
        if (net_wait_readable(d_sock, deadline - net_now_ms()) < 0) return TN_EOF;
    }
}

int l2d_exec(const char *cmd, int timeout_ms, char **out)
{
    char part[1024];
    const char *p = cmd;
    DBuf all;
    char *resp;
    int rc, attempt, status;

    if (out) *out = NULL;
    dbuf_init(&all);
    while ((p = l2d_next_part(p, part, sizeof(part))) != NULL) {
        for (attempt = 0; ; attempt++) {
            rc = l2d_begin(part);
            if (rc == -2) {
                dbuf_free(&all);
                set_err("command too long");
                return TN_EOF;
            }
            if (rc < 0) {
                dbuf_free(&all);
                return TN_EOF;
            }
            if (rc == 0) {
                rc = wait_response(timeout_ms);
                if (rc != TN_DONE) {
                    if (rc == TN_TIMEOUT) l2d_close();     /* (out of step now) */
                    dbuf_free(&all);
                    return rc;
                }
            }
            resp = l2d_take_response(&status);
            if (!l2d_is_busy(resp, status) || attempt >= L2D_BUSY_RETRIES) break;
            free(resp);
            sleep(L2D_BUSY_WAIT_MS / 1000);
        }
        if (all.len && *resp) dbuf_appendc(&all, '\n');
        dbuf_appends(&all, resp);
        free(resp);
    }
    if (out) *out = s_dup(all.data);
    dbuf_free(&all);
    return TN_DONE;
}
