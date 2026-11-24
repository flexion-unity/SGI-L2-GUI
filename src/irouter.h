/*
 * irouter.h - IRouter packets: the system controller's messages (SGI's
 * IRouter.c), carried between an L3 client and the L2 inside Router events.
 *
 * A packet is one or more frames, each big-endian on the wire:
 *   0 size u16 (the whole frame)   2 rev (2)      3 err        4 seqnum low
 *   5 fnum (0x80|n on the first of n frames, then n-1 .. 1)    6 clas  7 pdata
 *   8 dest u32   12 src u32   16 argc   17 seqnum high   18 opcode u16
 *   20 argc 8-byte arguments (first frame only), then the arguments' data.
 * An argument is: clas (low 3 bits: 0 value, 1 bytes, 2 string), frame
 * (1-based, where its data starts), size u16, val u32 (the value, or the
 * data's offset in that frame + 16). Data may run on into the following
 * frames, which carry a copy of the header with argc 0.
 *
 * Ported from SGI-L2-WUI backend/functions/irouter.js.
 */
#ifndef SGIL2_IROUTER_H
#define SGIL2_IROUTER_H

#include <stddef.h>
#include "util.h"

#define IR_FRAME_HDR    20
#define IR_FRAME_MAX    0x400       /* what a request may use */
#define IR_REV          2

#define IR_CLAS_REQ     0
#define IR_CLAS_RSP     1
#define IR_CLAS_EVT     2

#define IR_ARG_VALUE    0
#define IR_ARG_BYTES    1
#define IR_ARG_STRING   2

/* IR_ADDR_MAKE(type, rack, slot, num, task) */
#define IR_ADDR(type, rack, slot, num, task) \
    ((((unsigned long)(type)) << 28) | (((unsigned long)(rack)) << 18) | \
     (((unsigned long)(slot)) << 12) | (((unsigned long)(num)) << 5) | (unsigned long)(task))
#define IR_ADDR_L2_CMD  IR_ADDR(1, 0x3ff, 0x3f, 0, 3)   /* 0x1ffff003 */
#define IR_ADDR_L2_CFG  IR_ADDR(1, 0x3ff, 0x3f, 0, 4)   /* 0x1ffff004 */
#define IR_ADDR_L3      IR_ADDR(2, 0x3ff, 0x3f, 0, 0)   /* 0x2ffff000 */

/* an argument to encode: a value, or data (strings with their NUL) */
typedef struct {
    int                  type;
    unsigned long        value;
    const unsigned char *data;
    size_t               len;
} IrArgIn;

/* a decoded argument; data is malloc'd with an extra NUL after len bytes,
   so a string's text can be used as a C string */
typedef struct {
    int            type;
    unsigned long  value;
    unsigned char *data;
    size_t         len;
} IrArg;

typedef struct {
    int           rev;
    int           err;
    unsigned int  seqnum;
    int           clas;
    unsigned long dest;
    unsigned long src;
    unsigned int  opcode;
    int           argc;
    IrArg        *args;
} IrPacket;

/* append a single-frame packet to out; -1 if it does not fit a frame */
int  ir_encode(DBuf *out, int clas, unsigned int seqnum, unsigned long dest,
               unsigned long src, unsigned int opcode, int err,
               const IrArgIn *args, int nargs);

/* a packet from its frames (first to last); 0 or -1 with a message */
int  ir_decode(const unsigned char **frames, const size_t *lens, int nframes,
               IrPacket *pkt, char *err, size_t errlen);
void ir_packet_free(IrPacket *pkt);

/* SGI's IRouterErrorList (Logger.c) */
const char *ir_error_text(int err, char *buf, size_t size);

#endif
