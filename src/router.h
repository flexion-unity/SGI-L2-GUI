/*
 * router.h - SGI's Router protocol framing (Router.c, RouterCl.c): what an
 * L3 tool such as l2cmd speaks to an L2 on TCP port 8000.
 *
 * A packet is an 8-byte big-endian header (opcode u16, clas, id, config,
 * length u24: the bytes that follow) and then its arguments, each a
 * big-endian word clas<<24 | size and then, for clas 0, a 4-byte value or,
 * for clas 1, size bytes padded to a multiple of 4.
 *
 * Pure byte handling, no sockets (see l2direct.c), so it can be tested.
 * Ported from SGI-L2-WUI backend/functions/router.js.
 */
#ifndef SGIL2_ROUTER_H
#define SGIL2_ROUTER_H

#include <stddef.h>
#include "util.h"

/* packet classes */
#define RC_PASSWORD_REQ     0x01
#define RC_PASSWORD         0x02
#define RC_ACCEPT           0x04
#define RC_CLIENT_ID        0x08
#define RC_REQUEST          0x10
#define RC_DISCONNECT_REQ   0x11
#define RC_DISCONNECT_RSP   0x12
#define RC_CONFIG_INIT      0x14
#define RC_CONFIG_REQ       0x18
#define RC_REL_REQUEST      0x20
#define RC_CONFIG_RSP       0x21
#define RC_DENIED           0x22
#define RC_PING             0x24
#define RC_RESPONSE         0x40
#define RC_EVENT            0x80

#define RT_SERVER_PROCESS_ID 2      /* the L2's: EnetServerStart(port, 2, "L2") */
#define RT_RU_CONFIGURE      0x4000UL
#define RT_RH_COMPRESSED     0x80
#define RT_HDR               8
#define RT_MAX_ARGS          256

/* an argument: a value (isValue) or bytes */
typedef struct {
    int                  isValue;
    unsigned long        value;
    const unsigned char *data;
    size_t               len;
} RtArg;

/* a received packet; body and args point into the parser's buffer and
   stay valid until the next rt_next() */
typedef struct {
    unsigned int  opcode;
    int           clas;
    int           id;
    int           config;
    int           nargs;
    RtArg         args[RT_MAX_ARGS];
} RtPacket;

typedef struct {
    DBuf           in;          /* bytes received, not yet a whole packet */
    unsigned char *body;        /* the last packet's (uncompressed) body */
    size_t         bodyCap;
} RtParser;

/* append a packet with byte arguments (all clas 1, as SGI's clients send) */
void rt_pack(DBuf *out, unsigned int opcode, int clas, const RtArg *args, int nargs);

/*
 * RouterUnpack(): the arguments of a packet's body; returns their number.
 * Like the original, an argument word in the last 4 bytes is not read.
 */
int  rt_unpack_args(const unsigned char *body, size_t len, RtArg *out, int max);

/*
 * BuffUncompress(): a big-endian length, then bytes, where 0xfe n c is n
 * copies of c and 0xfd nn nn c the same with a 16-bit count.
 * *out is malloc'd. returns 0, or -1 if the data is bad.
 */
int  rt_uncompress(const unsigned char *in, size_t len, unsigned char **out, size_t *outlen);

/*
 * RouterPackID(): procname, username and fullname, each NUL-terminated,
 * then the IPv4 address in network order. The L2 only logs it.
 */
void rt_pack_id(DBuf *out, const char *procname, const char *user, const unsigned char ip[4]);

void rt_parser_init(RtParser *p);
void rt_parser_free(RtParser *p);
void rt_parser_reset(RtParser *p);
void rt_feed(RtParser *p, const unsigned char *data, size_t n);
/* 1: *pkt filled, 0: need more data, -1: bad packet (message in err) */
int  rt_next(RtParser *p, RtPacket *pkt, char *err, size_t errlen);

/* big-endian helpers */
unsigned long rt_get32(const unsigned char *p);
unsigned int  rt_get16(const unsigned char *p);
void          rt_put32(unsigned char *p, unsigned long v);
void          rt_put16(unsigned char *p, unsigned int v);

#endif
