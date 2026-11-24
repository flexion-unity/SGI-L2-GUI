/*
 * test_l2proto.c - host-side unit tests for the direct L2 protocol
 * (router.c, irouter.c, l2direct.c's command splitting).
 * Builds with any C89 compiler (gcc on Linux, MIPSpro on IRIX):
 *   make test
 *
 * The packets are Router traffic between SGI's l2gui and an L2, from
 * SGI-L2-WUI backend/test/vectors.js (argument padding zeroed there).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/router.h"
#include "../src/irouter.h"
#include "../src/l2direct.h"
#include "../src/util.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { failures++; \
    printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

/* the server's CONFIG_INIT (header and body as read) */
static const char *V_CONFIG_INIT = "000014000000000c010000080000000280000000";
/* l2gui's echo of it */
static const char *V_CONFIG_REQ = "000018000000000c010000080000000280000000";
/* l2gui's configuration query: IR request to 0x1ffff004 from 0x1ffff006, seqnum 0 */
static const char *V_CFG_QUERY =
    "000080000000003801000003495200000100002c00000000020001000000000000000000001c0200008100001ffff004"
    "1ffff006010000000000000000000000";
/* the L2's response: values 0 and 2, then the config and events texts */
static const char *V_CFG_RESPONSE =
    "00008000000001c80100000349520000010001bc0000000004000100000000000000000001ac02000081010086000101"
    "8400010104000000000000000000000000000000000000020a010158000000440a0100200000019c6c323a3132372e30"
    "2e312e312c6c327261636b3a302c6c326f733a6c696e75782c706f72743a302c706f72743a312c706f72743a322c706f"
    "72743a332c706f72743a342c706f72743a352c706f72743a362c706f72743a372c706f72743a382c706f72743a392c70"
    "6f72743a31302c706f72743a31312c706f72743a31322c706f72743a31332c706f72743a31342c706f72743a31352c70"
    "6f72743a31362c706f72743a31372c706f72743a31382c706f72743a31392c706f72743a32302c706f72743a32312c70"
    "6f72743a32322c706f72743a32332c706f72743a32342c706f72743a32352c706f72743a32362c706f72743a32372c70"
    "6f72743a32382c706f72743a32392c706f72743a33302c706f72743a33312c706f72743a33322c706f72743a33332c70"
    "6f72743a33342c706f72743a33352c706f72743a33362c706f72743a33372c706f72743a33382c706f72743a33392c00"
    "6c323a3132372e302e312e312c6c326576656e74733a66666666666665362c00";
/* an EV_L2_CONFIG event (opcode 1000) with the same texts as bytes */
static const char *V_CFG_EVENT =
    "00008000000001c80100000349520000010001bc0000000006000100000000000000000001ac02000181020000000000"
    "84000101040003e8000000000000000000000000000000020901015800000044090100200000019c6c323a3132372e30"
    "2e312e312c6c327261636b3a302c6c326f733a6c696e75782c706f72743a302c706f72743a312c706f72743a322c706f"
    "72743a332c706f72743a342c706f72743a352c706f72743a362c706f72743a372c706f72743a382c706f72743a392c70"
    "6f72743a31302c706f72743a31312c706f72743a31322c706f72743a31332c706f72743a31342c706f72743a31352c70"
    "6f72743a31362c706f72743a31372c706f72743a31382c706f72743a31392c706f72743a32302c706f72743a32312c70"
    "6f72743a32322c706f72743a32332c706f72743a32342c706f72743a32352c706f72743a32362c706f72743a32372c70"
    "6f72743a32382c706f72743a32392c706f72743a33302c706f72743a33312c706f72743a33322c706f72743a33332c70"
    "6f72743a33342c706f72743a33352c706f72743a33362c706f72743a33372c706f72743a33382c706f72743a33392c00"
    "6c323a3132372e302e312e312c6c326576656e74733a66666666666665362c00";
/* l2gui's command "p <none> power down" to 0x1ffff003, seqnum 1 */
static const char *V_POWER_REQ =
    "00008000000000540100000349520000010000480000000002000100000000000000000000380200018100001ffff003"
    "1ffff006020000000000000000000000020100140000003470203c6e6f6e653e20706f77657220646f776e00";
/* its response: an error text */
static const char *V_POWER_RSP =
    "000080000000007001000003495200000100006300000000040001000000000000000000005302000181010086000101"
    "830001010200000000000000000000000a01002f000000344552524f523a206e6f204c317320666f756e6420746f206d"
    "6174636820746861742064657374696e6174696f6e0a0000";
/* l2cmd's request for "version" (worked out from SCapi.c) */
static const char *V_VERSION_REQ =
    "000080000000004c" "0100000349520000" "0100003d" "00000000020001000000000000000000"
    "002d020000810000" "1ffff003" "2ffff000" "02000000" "0000000000000000"
    "0201000900000034" "76657273696f6e2000" "000000";

static const unsigned char PREFIX[16] = {
    0, 0, 0, 0, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* hex -> malloc'd bytes */
static unsigned char *unhex(const char *h, size_t *len)
{
    size_t n = strlen(h) / 2, i;
    unsigned char *b = (unsigned char *)xmalloc(n + 1);
    unsigned int v;
    for (i = 0; i < n; i++) {
        sscanf(h + 2 * i, "%2x", &v);
        b[i] = (unsigned char)v;
    }
    *len = n;
    return b;
}

static int same_bytes(const DBuf *b, const char *hex)
{
    size_t n;
    unsigned char *want = unhex(hex, &n);
    int ok = b->len == n && memcmp(b->data, want, n) == 0;
    free(want);
    return ok;
}

/* a Router event packet carrying one IRouter frame, as the client sends it */
static void ir_event(DBuf *out, int clas, unsigned int seq, unsigned long dest,
                     unsigned long src, const IrArgIn *args, int nargs)
{
    DBuf frame;
    RtArg ra[2];
    dbuf_init(&frame);
    dbuf_append(&frame, (const char *)PREFIX, 16);
    CHECK(ir_encode(&frame, clas, seq, dest, src, 0, 0, args, nargs) == 0);
    ra[0].isValue = 0;
    ra[0].data = (const unsigned char *)"IR";
    ra[0].len = 3;
    ra[1].isValue = 0;
    ra[1].data = (const unsigned char *)frame.data;
    ra[1].len = frame.len;
    rt_pack(out, 0, RC_EVENT, ra, 2);
    dbuf_free(&frame);
}

static RtPacket pkt;

/* parse one whole Router packet from hex */
static int parse_hex(RtParser *p, const char *hex)
{
    size_t n;
    unsigned char *b = unhex(hex, &n);
    char err[128];
    int rc;
    rt_parser_reset(p);
    rt_feed(p, b, n);
    free(b);
    rc = rt_next(p, &pkt, err, sizeof(err));
    return rc;
}

/* the IRouter packet in a received Router event */
static int ir_from_event(RtParser *p, const char *hex, IrPacket *ip)
{
    const unsigned char *frames[RT_MAX_ARGS];
    size_t lens[RT_MAX_ARGS];
    char err[128];
    int i;
    if (parse_hex(p, hex) != 1) return -1;
    if (pkt.clas != RC_EVENT || pkt.nargs < 2) return -1;
    if (pkt.args[0].len != 3 || memcmp(pkt.args[0].data, "IR\0", 3) != 0) return -1;
    for (i = 1; i < pkt.nargs; i++) {
        frames[i - 1] = pkt.args[i].data + 16;
        lens[i - 1] = pkt.args[i].len - 16;
    }
    return ir_decode(frames, lens, pkt.nargs - 1, ip, err, sizeof(err));
}

static void test_config_echo(RtParser *p)
{
    DBuf out;
    CHECK(parse_hex(p, V_CONFIG_INIT) == 1);
    CHECK(pkt.clas == RC_CONFIG_INIT);
    CHECK(pkt.nargs == 1);
    dbuf_init(&out);
    rt_pack(&out, 0, RC_CONFIG_REQ, pkt.args, 1);
    CHECK(same_bytes(&out, V_CONFIG_REQ));
    dbuf_free(&out);
}

static void test_requests(void)
{
    DBuf out;
    IrArgIn a[2];
    const char *cmd = "p <none> power down";
    const char *ver = "version ";

    a[0].type = IR_ARG_VALUE;
    a[0].value = 0;
    a[0].data = NULL;
    a[0].len = 0;
    dbuf_init(&out);
    ir_event(&out, IR_CLAS_REQ, 0, IR_ADDR_L2_CFG, 0x1ffff006UL, a, 1);
    CHECK(same_bytes(&out, V_CFG_QUERY));

    a[1].type = IR_ARG_STRING;
    a[1].value = 0;
    a[1].data = (const unsigned char *)cmd;
    a[1].len = strlen(cmd) + 1;
    dbuf_clear(&out);
    ir_event(&out, IR_CLAS_REQ, 1, IR_ADDR_L2_CMD, 0x1ffff006UL, a, 2);
    CHECK(same_bytes(&out, V_POWER_REQ));

    a[1].data = (const unsigned char *)ver;
    a[1].len = strlen(ver) + 1;
    dbuf_clear(&out);
    ir_event(&out, IR_CLAS_REQ, 0, IR_ADDR_L2_CMD, IR_ADDR_L3, a, 2);
    CHECK(same_bytes(&out, V_VERSION_REQ));
    dbuf_free(&out);

    CHECK(IR_ADDR_L2_CMD == 0x1ffff003UL);
    CHECK(IR_ADDR_L3 == 0x2ffff000UL);
}

static void test_responses(RtParser *p)
{
    IrPacket ip;

    CHECK(ir_from_event(p, V_CFG_RESPONSE, &ip) == 0);
    CHECK(ip.clas == IR_CLAS_RSP);
    CHECK(ip.seqnum == 0);
    CHECK(ip.err == 0);
    CHECK(ip.src == 0x84000101UL);
    CHECK(ip.argc == 4);
    if (ip.argc == 4) {
        CHECK(ip.args[0].type == IR_ARG_VALUE && ip.args[0].value == 0);
        CHECK(ip.args[1].type == IR_ARG_VALUE && ip.args[1].value == 2);
        CHECK(s_startswith((const char *)ip.args[2].data, "l2:127.0.1.1,l2rack:0,l2os:linux,port:0,"));
        CHECK(s_endswith((const char *)ip.args[2].data, "port:39,"));
        CHECK_STR((const char *)ip.args[3].data, "l2:127.0.1.1,l2events:ffffffe6,");
    }
    ir_packet_free(&ip);

    CHECK(ir_from_event(p, V_CFG_EVENT, &ip) == 0);
    CHECK(ip.clas == IR_CLAS_EVT);
    CHECK(ip.opcode == 1000);
    CHECK(ip.argc >= 3 && ip.args[2].type == IR_ARG_BYTES);
    if (ip.argc >= 3) CHECK(s_startswith((const char *)ip.args[2].data, "l2:127.0.1.1,"));
    ir_packet_free(&ip);

    CHECK(ir_from_event(p, V_POWER_RSP, &ip) == 0);
    CHECK(ip.clas == IR_CLAS_RSP);
    CHECK(ip.seqnum == 1);
    CHECK(ip.src == 0x83000101UL);
    CHECK(ip.argc >= 2);
    if (ip.argc >= 2)
        CHECK_STR((const char *)ip.args[1].data, "ERROR: no L1s found to match that destination\n");
    ir_packet_free(&ip);
}

/* two frames: the string starts in the first and ends in the second */
static void test_multiframe(void)
{
    const char *text = "hello, world";       /* + NUL = 13 bytes */
    unsigned char f1[20 + 8 + 5], f2[20 + 13 - 5];
    const unsigned char *frames[2];
    size_t lens[2];
    IrPacket ip;
    char err[128];

    memset(f1, 0, sizeof(f1));
    rt_put16(f1, sizeof(f1));
    f1[2] = 2;
    f1[5] = 0x82;
    f1[6] = IR_CLAS_RSP;
    f1[16] = 1;
    f1[20] = 0x0a;
    f1[21] = 1;
    rt_put16(f1 + 22, 13);
    rt_put32(f1 + 24, 28 + 16);
    memcpy(f1 + 28, text, 5);
    memcpy(f2, f1, 20);
    rt_put16(f2, sizeof(f2));
    f2[5] = 1;
    f2[16] = 0;
    memcpy(f2 + 20, text + 5, 8);   /* "world" + ... + NUL */
    frames[0] = f1;
    frames[1] = f2;
    lens[0] = sizeof(f1);
    lens[1] = sizeof(f2);
    CHECK(ir_decode(frames, lens, 2, &ip, err, sizeof(err)) == 0);
    CHECK(ip.argc == 1 && ip.args[0].type == IR_ARG_STRING);
    if (ip.argc == 1) CHECK_STR((const char *)ip.args[0].data, "hello, world");
    ir_packet_free(&ip);

    /* data that runs past the last frame is refused */
    CHECK(ir_decode(frames, lens, 1, &ip, err, sizeof(err)) < 0);
}

static void test_too_large(void)
{
    static unsigned char big[2000];
    IrArgIn a;
    DBuf out;
    a.type = IR_ARG_STRING;
    a.value = 0;
    a.data = big;
    a.len = sizeof(big);
    dbuf_init(&out);
    CHECK(ir_encode(&out, 0, 0, 0, 0, 0, 0, &a, 1) < 0);
    dbuf_free(&out);
}

static void test_unpack_trailing(void)
{
    DBuf out;
    RtArg a[2], got[4];
    dbuf_init(&out);
    a[0].isValue = 0;
    a[0].data = (const unsigned char *)"IR";
    a[0].len = 3;
    a[1].isValue = 0;
    a[1].data = (const unsigned char *)"";
    a[1].len = 0;
    rt_pack(&out, 0, RC_EVENT, a, 2);
    /* (an argument word in the last 4 bytes is not read, as RouterUnpack()) */
    CHECK(rt_unpack_args((const unsigned char *)out.data + 8, out.len - 8, got, 4) == 1);
    dbuf_free(&out);
}

static void test_uncompress(RtParser *p)
{
    size_t n, outlen, i;
    unsigned char *c, *out;
    int ok;
    DBuf pk;
    char err[128];
    unsigned char hdr[8], body[16];

    /* 'a' x 10, 'b', 0xfe x 300 (a 16-bit run), 'c' */
    c = unhex("0000013a" "fe0a61" "62" "fd012cfe" "63", &n);
    CHECK(rt_uncompress(c, n, &out, &outlen) == 0);
    CHECK(outlen == 312);
    ok = outlen == 312;
    for (i = 0; ok && i < 10; i++) ok = out[i] == 'a';
    ok = ok && out[10] == 'b';
    for (i = 11; ok && i < 311; i++) ok = out[i] == 0xfe;
    ok = ok && out[311] == 'c';
    CHECK(ok);
    free(out);
    free(c);

    c = unhex("00000002fe0a61", &n);
    CHECK(rt_uncompress(c, n, &out, &outlen) < 0);
    free(c);

    /* a compressed CONFIG_INIT through the parser */
    memcpy(body, "\0\0\0\x0c\x01\0\0\x08\0\0\0\x02\x80\0\0\0", 16);
    memset(hdr, 0, sizeof(hdr));
    rt_put16(hdr, 0);
    hdr[2] = RC_CONFIG_INIT;
    hdr[4] = RT_RH_COMPRESSED;
    hdr[7] = 16;
    dbuf_init(&pk);
    dbuf_append(&pk, (const char *)hdr, 8);
    dbuf_append(&pk, (const char *)body, 16);
    rt_parser_reset(p);
    rt_feed(p, (const unsigned char *)pk.data, pk.len);
    CHECK(rt_next(p, &pkt, err, sizeof(err)) == 1);
    CHECK(pkt.clas == RC_CONFIG_INIT && pkt.nargs == 1 && pkt.args[0].len == 8);
    dbuf_free(&pk);
}

/* packets arriving a byte at a time, and two in one read */
static void test_parser_chunks(RtParser *p)
{
    size_t n, i, m;
    unsigned char *b = unhex(V_POWER_RSP, &n), *two;
    char err[128];
    int got = 0;

    rt_parser_reset(p);
    for (i = 0; i < n; i++) {
        rt_feed(p, b + i, 1);
        if (rt_next(p, &pkt, err, sizeof(err)) == 1) got++;
    }
    CHECK(got == 1);
    CHECK(pkt.clas == RC_EVENT && pkt.nargs == 2);

    m = n;
    two = (unsigned char *)xmalloc(2 * m);
    memcpy(two, b, m);
    memcpy(two + m, b, m);
    rt_parser_reset(p);
    rt_feed(p, two, 2 * m);
    CHECK(rt_next(p, &pkt, err, sizeof(err)) == 1);
    CHECK(rt_next(p, &pkt, err, sizeof(err)) == 1);
    CHECK(rt_next(p, &pkt, err, sizeof(err)) == 0);
    free(two);
    free(b);
}

static void test_pack_id(void)
{
    DBuf out;
    static const unsigned char ip[4] = { 192, 168, 0, 5 };
    dbuf_init(&out);
    rt_pack_id(&out, "sgil2gui", "averyveryverylongname", ip);
    /* "sgil2gui\0" "averyver\0" "\0" c0 a8 00 05 */
    CHECK(out.len == 9 + 9 + 1 + 4);
    CHECK(memcmp(out.data, "sgil2gui\0averyver\0\0\xc0\xa8\0\x05", 23) == 0);
    dbuf_free(&out);
}

static void test_parts(void)
{
    char part[64];
    const char *p = "  brick ; ; 001c01 display;power up  ";
    p = l2d_next_part(p, part, sizeof(part));
    CHECK(p != NULL);
    CHECK_STR(part, "brick");
    p = l2d_next_part(p, part, sizeof(part));
    CHECK(p != NULL);
    CHECK_STR(part, "001c01 display");
    p = l2d_next_part(p, part, sizeof(part));
    CHECK(p != NULL);
    CHECK_STR(part, "power up");
    CHECK(l2d_next_part(p, part, sizeof(part)) == NULL);
    CHECK(l2d_next_part("  ;  ", part, sizeof(part)) == NULL);

    CHECK(l2d_is_busy("ERROR: command library is busy", -1));
    CHECK(!l2d_is_busy("command library is busy", 0));
    CHECK(!l2d_is_busy("ERROR: something else", -1));
}

int main(void)
{
    RtParser p;
    rt_parser_init(&p);

    test_config_echo(&p);
    test_requests();
    test_responses(&p);
    test_multiframe();
    test_too_large();
    test_unpack_trailing();
    test_uncompress(&p);
    test_parser_chunks(&p);
    test_pack_id();
    test_parts();

    rt_parser_free(&p);
    printf("test_l2proto: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
