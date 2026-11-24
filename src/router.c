/*
 * router.c - SGI's Router protocol framing (see router.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "router.h"
#include "util.h"

unsigned long rt_get32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

unsigned int rt_get16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

void rt_put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)((v >> 24) & 0xff);
    p[1] = (unsigned char)((v >> 16) & 0xff);
    p[2] = (unsigned char)((v >> 8) & 0xff);
    p[3] = (unsigned char)(v & 0xff);
}

void rt_put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)((v >> 8) & 0xff);
    p[1] = (unsigned char)(v & 0xff);
}

static size_t pad4(size_t n)
{
    return (n + 3) & ~(size_t)3;
}

void rt_pack(DBuf *out, unsigned int opcode, int clas, const RtArg *args, int nargs)
{
    unsigned char hdr[RT_HDR], w[4];
    static const char zeros[4] = { 0, 0, 0, 0 };
    size_t length = 0;
    int i;

    for (i = 0; i < nargs; i++) length += 4 + pad4(args[i].len);
    rt_put16(hdr, opcode);
    hdr[2] = (unsigned char)clas;
    hdr[3] = 0;
    hdr[4] = 0;
    hdr[5] = (unsigned char)((length >> 16) & 0xff);
    hdr[6] = (unsigned char)((length >> 8) & 0xff);
    hdr[7] = (unsigned char)(length & 0xff);
    dbuf_append(out, (const char *)hdr, RT_HDR);
    for (i = 0; i < nargs; i++) {
        rt_put32(w, (1UL << 24) | (unsigned long)args[i].len);
        dbuf_append(out, (const char *)w, 4);
        if (args[i].len) dbuf_append(out, (const char *)args[i].data, args[i].len);
        dbuf_append(out, zeros, pad4(args[i].len) - args[i].len);
    }
}

int rt_unpack_args(const unsigned char *body, size_t len, RtArg *out, int max)
{
    size_t p = 0, size;
    unsigned long w;
    int n = 0;

    while (p + 4 < len && n < max) {
        w = rt_get32(body + p);
        size = (size_t)(w & 0xffffffUL);
        p += 4;
        if ((w >> 24) == 0) {
            if (p + 4 > len) break;
            out[n].isValue = 1;
            out[n].value = rt_get32(body + p);
            out[n].data = NULL;
            out[n].len = 0;
            p += 4;
        } else {
            if (p + size > len) break;
            out[n].isValue = 0;
            out[n].value = 0;
            out[n].data = body + p;
            out[n].len = size;
            p += pad4(size);
        }
        n++;
    }
    return n;
}

int rt_uncompress(const unsigned char *in, size_t len, unsigned char **out, size_t *outlen)
{
    size_t size, o = 0, i = 4, n;
    unsigned char c, *buf;

    *out = NULL;
    *outlen = 0;
    if (len < 4) return -1;
    size = (size_t)rt_get32(in);
    buf = (unsigned char *)xmalloc(size + 1);
    while (i < len) {
        c = in[i++];
        n = 1;
        if (c == 0xfe || c == 0xfd) {
            if (len - i < (size_t)(c == 0xfe ? 2 : 3)) {
                free(buf);
                return -1;
            }
            if (c == 0xfe) {
                n = in[i];
                i += 1;
            } else {
                n = rt_get16(in + i);
                i += 2;
            }
            c = in[i++];
        }
        if (n > size - o) {
            free(buf);
            return -1;
        }
        memset(buf + o, c, n);
        o += n;
    }
    *out = buf;
    *outlen = o;
    return 0;
}

static void append_cstr(DBuf *out, const char *s, size_t max)
{
    size_t n = s ? strlen(s) : 0;
    if (n > max) n = max;
    if (n) dbuf_append(out, s, n);
    dbuf_append(out, "", 1);
}

void rt_pack_id(DBuf *out, const char *procname, const char *user, const unsigned char ip[4])
{
    append_cstr(out, procname, 16);
    append_cstr(out, user, 8);
    dbuf_append(out, "", 1);                    /* (no full name) */
    dbuf_append(out, (const char *)ip, 4);
}

void rt_parser_init(RtParser *p)
{
    dbuf_init(&p->in);
    p->body = NULL;
    p->bodyCap = 0;
}

void rt_parser_free(RtParser *p)
{
    dbuf_free(&p->in);
    free(p->body);
    p->body = NULL;
    p->bodyCap = 0;
}

void rt_parser_reset(RtParser *p)
{
    if (!p->in.data) dbuf_init(&p->in);
    dbuf_clear(&p->in);
}

void rt_feed(RtParser *p, const unsigned char *data, size_t n)
{
    if (!p->in.data) dbuf_init(&p->in);
    dbuf_append(&p->in, (const char *)data, n);
}

int rt_next(RtParser *p, RtPacket *pkt, char *err, size_t errlen)
{
    const unsigned char *b = (const unsigned char *)p->in.data;
    size_t length, blen;
    unsigned char *un;
    size_t unlen;

    if (!b || p->in.len < RT_HDR) return 0;
    length = ((size_t)b[5] << 16) | ((size_t)b[6] << 8) | (size_t)b[7];
    if (p->in.len < RT_HDR + length) return 0;

    pkt->opcode = rt_get16(b);
    pkt->clas = b[2];
    pkt->id = b[3];
    pkt->config = b[4];

    if (pkt->config & RT_RH_COMPRESSED) {
        if (rt_uncompress(b + RT_HDR, length, &un, &unlen) < 0) {
            s_copy(err, "bad compressed packet from the L2", errlen);
            return -1;
        }
        free(p->body);
        p->body = un;
        p->bodyCap = unlen + 1;
        blen = unlen;
    } else {
        if (p->bodyCap < length + 1) {
            free(p->body);
            p->bodyCap = length + 1;
            p->body = (unsigned char *)xmalloc(p->bodyCap);
        }
        memcpy(p->body, b + RT_HDR, length);
        blen = length;
    }
    /* consume the packet */
    memmove(p->in.data, p->in.data + RT_HDR + length, p->in.len - RT_HDR - length);
    p->in.len -= RT_HDR + length;
    p->in.data[p->in.len] = '\0';

    pkt->nargs = rt_unpack_args(p->body, blen, pkt->args, RT_MAX_ARGS);
    return 1;
}
