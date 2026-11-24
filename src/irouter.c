/*
 * irouter.c - IRouter packets (see irouter.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irouter.h"
#include "router.h"
#include "util.h"

static const char *ir_errors[] = {
    "NO_ROUTER_ERROR", "bad packet type", "bad task ID", "duplicate target ID", "malloc failed",
    "duplicate task ID", "bad event entry", "bad argument list argument count",
    "no receiver for packet", "no device to read/write", "attempt to send to full queue",
    "queue is full", "read failed - read error", "read failed - not first frame",
    "read failed - unexpected frame", "read failed - select() error",
    "read timeout on multiframe packet", "write failed", "sequence ID mismatch",
    "wait further for response", "timeout waiting for packet", "read failed - end of file",
    "write failed - end of file (zero bytes written) ", "framing error, frame incomplete",
    "framing error, frame overflow", "framing error, frame underflow",
    "framing error, frame control", "response packet is too large"
};

const char *ir_error_text(int err, char *buf, size_t size)
{
    if (err >= 0 && err < (int)(sizeof(ir_errors) / sizeof(ir_errors[0])))
        return ir_errors[err];
    sprintf(buf, "IRouter error %d", err);
    (void)size;
    return buf;
}

int ir_encode(DBuf *out, int clas, unsigned int seqnum, unsigned long dest,
              unsigned long src, unsigned int opcode, int err,
              const IrArgIn *args, int nargs)
{
    unsigned char *f;
    size_t dataStart = IR_FRAME_HDR + 8 * (size_t)nargs, size = dataStart, off, p;
    int i;

    if (nargs > 255) return -1;
    for (i = 0; i < nargs; i++)
        if (args[i].type != IR_ARG_VALUE) size += args[i].len;
    if (size > IR_FRAME_MAX) return -1;

    f = (unsigned char *)xmalloc(size);
    memset(f, 0, size);
    rt_put16(f, (unsigned int)size);
    f[2] = IR_REV;
    f[3] = (unsigned char)err;
    f[4] = (unsigned char)(seqnum & 0xff);
    f[5] = 0x81;
    f[6] = (unsigned char)clas;
    f[7] = 0;
    rt_put32(f + 8, dest);
    rt_put32(f + 12, src);
    f[16] = (unsigned char)nargs;
    f[17] = (unsigned char)((seqnum >> 8) & 0xff);
    rt_put16(f + 18, opcode);
    off = dataStart;
    for (i = 0; i < nargs; i++) {
        p = IR_FRAME_HDR + 8 * (size_t)i;
        if (args[i].type == IR_ARG_VALUE) {
            rt_put32(f + p + 4, args[i].value);
            continue;
        }
        f[p] = (unsigned char)args[i].type;
        f[p + 1] = 1;
        rt_put16(f + p + 2, (unsigned int)args[i].len);
        rt_put32(f + p + 4, (unsigned long)(off + 16));
        if (args[i].len) memcpy(f + off, args[i].data, args[i].len);
        off += args[i].len;
    }
    dbuf_append(out, (const char *)f, size);
    free(f);
    return 0;
}

/* IRArg(): size bytes from frame fnum (1-based) at offset val - 16, running
   on into the following frames' bytes after their header */
static int arg_data(const unsigned char **frames, const size_t *lens, int nframes,
                    int fnum, size_t size, unsigned long val, IrArg *a,
                    char *err, size_t errlen)
{
    int i = fnum - 1;
    size_t start, left = size, n, o = 0;

    if (i < 0 || i >= nframes || val < 16) {
        s_copy(err, "IRouter argument in a missing frame", errlen);
        return -1;
    }
    start = (size_t)(val - 16);
    a->data = (unsigned char *)xmalloc(size + 1);
    a->len = size;
    while (left > 0) {
        if (i >= nframes || start > lens[i]) {
            free(a->data);
            a->data = NULL;
            s_copy(err, "IRouter argument runs past its packet", errlen);
            return -1;
        }
        n = lens[i] - start;
        if (n > left) n = left;
        memcpy(a->data + o, frames[i] + start, n);
        o += n;
        left -= n;
        i++;
        start = IR_FRAME_HDR;
    }
    a->data[size] = '\0';
    return 0;
}

int ir_decode(const unsigned char **frames, const size_t *lens, int nframes,
              IrPacket *pkt, char *err, size_t errlen)
{
    const unsigned char *f;
    int i, argc;
    size_t p;

    memset(pkt, 0, sizeof(*pkt));
    if (nframes < 1 || lens[0] < IR_FRAME_HDR) {
        s_copy(err, "IRouter frame too short", errlen);
        return -1;
    }
    f = frames[0];
    pkt->rev = f[2];
    pkt->err = f[3];
    pkt->seqnum = ((unsigned int)f[17] << 8) | f[4];
    pkt->clas = f[6];
    pkt->dest = rt_get32(f + 8);
    pkt->src = rt_get32(f + 12);
    pkt->opcode = rt_get16(f + 18);
    argc = f[16];
    if (lens[0] < IR_FRAME_HDR + 8 * (size_t)argc) {
        s_copy(err, "IRouter frame too short for its arguments", errlen);
        return -1;
    }
    pkt->args = (IrArg *)xmalloc(sizeof(IrArg) * (size_t)(argc ? argc : 1));
    for (i = 0; i < argc; i++) {
        IrArg *a = &pkt->args[i];
        p = IR_FRAME_HDR + 8 * (size_t)i;
        a->type = f[p] & 7;
        a->value = rt_get32(f + p + 4);
        a->data = NULL;
        a->len = 0;
        pkt->argc = i;
        if (a->type == IR_ARG_VALUE) {
            pkt->argc = i + 1;
            continue;
        }
        if (arg_data(frames, lens, nframes, f[p + 1], rt_get16(f + p + 2),
                     a->value, a, err, errlen) < 0) {
            ir_packet_free(pkt);
            return -1;
        }
        a->value = 0;
        pkt->argc = i + 1;
    }
    pkt->argc = argc;
    return 0;
}

void ir_packet_free(IrPacket *pkt)
{
    int i;
    if (!pkt->args) return;
    for (i = 0; i < pkt->argc; i++) free(pkt->args[i].data);
    free(pkt->args);
    pkt->args = NULL;
    pkt->argc = 0;
}
