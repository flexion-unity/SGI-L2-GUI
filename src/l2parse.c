/*
 * l2parse.c - parsers for SGI L2 controller command output
 *
 * Port of SGI-L2-WUI backend/functions/telnet-api.js
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "l2parse.h"
#include "util.h"

/*
 * Iterate over the lines of a text block. Copies the text, strips non
 * printable characters (NL4R fix from the WUI), splits on '\n' and calls
 * fn for every non-empty, trimmed line.
 */
typedef void (*LineFn)(char *row, void *ctx);

static void for_each_line(const char *txt, LineFn fn, void *ctx)
{
    char *copy, *p, *nl, *row;
    if (!txt) return;
    copy = s_dup(txt);
    s_strip_nonprint(copy);
    p = copy;
    while (p && *p) {
        nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        row = s_trim(p);
        if (*row) fn(row, ctx);
        p = nl ? nl + 1 : NULL;
    }
    free(copy);
}

/* "001c01:" - a 6 char id followed by ':' starts a new device section */
static int is_section_header(const char *row)
{
    return strlen(row) == 7 && row[6] == ':';
}

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    L2ConfigEntry *out;
    int max;
    int n;
} ConfigCtx;

/* return the idx'th space separated token of row (0 based) */
static void nth_token(const char *row, int idx, char *dst, size_t size)
{
    const char *p = row, *start;
    int i = 0;
    dst[0] = '\0';
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (i == idx) {
            s_ncopy(dst, start, (size_t)(p - start), size);
            return;
        }
        i++;
    }
}

static void config_line(char *row, void *vctx)
{
    ConfigCtx *ctx = (ConfigCtx *)vctx;
    L2ConfigEntry *e;
    const char *dash;
    char tmp[64];

    if (ctx->n >= ctx->max) return;
    e = &ctx->out[ctx->n];
    memset(e, 0, sizeof(*e));

    if (s_startswith(row, "L1")) {
        /* L1 172.16.9.1:0:0 - 001c01 */
        s_copy(e->ltype, "L1", sizeof(e->ltype));
        nth_token(row, 1, e->addr, sizeof(e->addr));
        dash = strchr(row, '-');
        if (dash) {
            s_copy(tmp, dash + 1, sizeof(tmp));
            s_copy(e->id, s_trim(tmp), sizeof(e->id));
        }
    } else if (s_startswith(row, "L2")) {
        /* L2 172.16.9.1: - 001 (LOCAL) */
        s_copy(e->ltype, "L2", sizeof(e->ltype));
        nth_token(row, 1, e->addr, sizeof(e->addr));
        nth_token(row, 3, e->id, sizeof(e->id));
    } else {
        return; /* unknown config line */
    }
    s_copy(e->raw, row, sizeof(e->raw));
    ctx->n++;
}

int l2_parse_config(const char *txt, L2ConfigEntry *out, int max)
{
    ConfigCtx ctx;
    ctx.out = out;
    ctx.max = max;
    ctx.n = 0;
    for_each_line(txt, config_line, &ctx);
    return ctx.n;
}

/* ------------------------------------------------------------------ */
/* display                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    L2Display *out;
    int max;
    int n;
    L2Display *cur;
} DisplayCtx;

/* copy text between the first and second '"' */
static void quoted(const char *row, char *dst, size_t size)
{
    const char *a = strchr(row, '"'), *b;
    dst[0] = '\0';
    if (!a) return;
    a++;
    b = strchr(a, '"');
    if (!b) b = a + strlen(a);
    s_ncopy(dst, a, (size_t)(b - a), size);
}

static void display_line(char *row, void *vctx)
{
    DisplayCtx *ctx = (DisplayCtx *)vctx;
    L2Display *d;

    if (is_section_header(row)) {
        if (ctx->n >= ctx->max) {
            ctx->cur = NULL;
            return;
        }
        d = &ctx->out[ctx->n++];
        memset(d, 0, sizeof(*d));
        s_ncopy(d->id, row, 6, sizeof(d->id));
        ctx->cur = d;
        return;
    }
    d = ctx->cur;
    if (!d) return;
    if (s_startswith(row, "revision:")) {
        s_copy(d->revision, s_trim(row + 9), sizeof(d->revision));
    } else if (s_startswith(row, "line 1:")) {
        quoted(row, d->line1, sizeof(d->line1));
    } else if (s_startswith(row, "line 2:")) {
        quoted(row, d->line2, sizeof(d->line2));
    } else if (s_startswith(row, "leds:")) {
        d->pwr = strstr(row, "PWR=on") != NULL;
        d->service = strstr(row, "SERVICE=on") != NULL;
        d->fault = strstr(row, "FAULT=on") != NULL;
    }
}

int l2_parse_display(const char *txt, L2Display *out, int max)
{
    DisplayCtx ctx;
    ctx.out = out;
    ctx.max = max;
    ctx.n = 0;
    ctx.cur = NULL;
    for_each_line(txt, display_line, &ctx);
    return ctx.n;
}

/* ------------------------------------------------------------------ */
/* brick                                                               */
/* ------------------------------------------------------------------ */

const char *l2_skin_name(int skin)
{
    static const char *names[SKIN_COUNT] = {
        "o300", "o350", "tezro", "yellow", "grey",
        "a3000", "a3000ix", "nl4r", "router", "router-altix"
    };
    if (skin < 0 || skin >= SKIN_COUNT) return "?";
    return names[skin];
}

/*
 * Known type values:
 *   "C (IP45) [2MB flash]"        = Origin 300
 *   "...Rackmount)..."            = Tezro rackmount
 *   "Chimera Blade [2MB flash]"   = Onyx 350
 *   "Chimera Server [2MB flash]"  = Origin 350
 *   "Opus [2MB flash]"            = Altix 350
 *   "NL4R [2MB flash]"            = NL4R router
 *   "C (IP41) [2MB flash]"        = Altix 3000
 *   "IX [2MB flash]"              = Altix 3000 iX
 */
void l2_classify(L2Brick *b, const L2ParseOpts *opts)
{
    const char *t = b->type;
    int routerAltix = opts ? opts->routerAltix : 0;
    int grey = opts ? opts->altix350Grey : 0;

    b->brickType = (char)toupper((unsigned char)(strlen(b->id) > 3 ? b->id[3] : '?'));
    b->productLabel[0] = '\0';

    if (b->brickType == 'R') {
        if (s_startswith(t, "NL4R")) {
            b->skin = SKIN_NL4R;
            s_copy(b->productLabel, "sgi nl4r", sizeof(b->productLabel));
        } else if (routerAltix) {
            b->skin = SKIN_ROUTER_ALTIX;
        } else {
            b->skin = SKIN_ROUTER;
        }
        return;
    }
    if (s_startswith(t, "C (IP45)")) {
        b->skin = SKIN_O300;
        s_copy(b->productLabel, "sgi origin 300", sizeof(b->productLabel));
    } else if (s_contains_ci(t, "rackmount")) {
        b->skin = SKIN_TEZRO;
        s_copy(b->productLabel, "silicon graphics tezro", sizeof(b->productLabel));
    } else if (s_contains_ci(t, "chimera blade")) {
        b->skin = SKIN_O350;
        s_copy(b->productLabel, "sgi onyx 350", sizeof(b->productLabel));
    } else if (s_startswith(t, "Opus")) {
        b->skin = grey ? SKIN_A350_GREY : SKIN_A350_YELLOW;
        s_copy(b->productLabel, "sgi altix 350", sizeof(b->productLabel));
    } else if (s_startswith(t, "C (IP41)")) {
        b->skin = SKIN_A3000;
        s_copy(b->productLabel, "sgi altix 3000", sizeof(b->productLabel));
    } else if (s_startswith(t, "IX")) {
        b->skin = SKIN_A3000IX;
        s_copy(b->productLabel, "sgi altix iX brick", sizeof(b->productLabel));
    } else {
        /* Chimera server and everything unknown */
        b->skin = SKIN_O350;
        s_copy(b->productLabel, "sgi origin 350", sizeof(b->productLabel));
    }
}

typedef struct {
    L2Brick *out;
    int max;
    int n;
    L2Brick *cur;
    const L2ParseOpts *opts;
} BrickCtx;

static void brick_field(L2Brick *b, const char *key, const char *val)
{
    if (strcmp(key, "rack") == 0) s_copy(b->rack, val, sizeof(b->rack));
    else if (strcmp(key, "slot") == 0) s_copy(b->slot, val, sizeof(b->slot));
    else if (strcmp(key, "partition") == 0) s_copy(b->partition, val, sizeof(b->partition));
    else if (strcmp(key, "type") == 0) s_copy(b->type, val, sizeof(b->type));
    else if (strcmp(key, "serial") == 0) s_copy(b->serial, val, sizeof(b->serial));
    else if (strcmp(key, "source") == 0) s_copy(b->source, val, sizeof(b->source));
}

static void brick_line(char *row, void *vctx)
{
    BrickCtx *ctx = (BrickCtx *)vctx;
    L2Brick *b;
    char *entry, *next, *colon, *key, *val;

    if (is_section_header(row)) {
        if (ctx->n >= ctx->max) {
            ctx->cur = NULL;
            return;
        }
        b = &ctx->out[ctx->n++];
        memset(b, 0, sizeof(*b));
        s_ncopy(b->id, row, 6, sizeof(b->id));
        l2_classify(b, ctx->opts);
        ctx->cur = b;
        return;
    }
    b = ctx->cur;
    if (!b) return;
    if (s_startswith(row, "rack:")) {
        /* rack: 001, slot: 01, partition: none, type: C (IP45) [2MB flash], serial:NCC120, source: EEPROM */
        entry = row;
        while (entry) {
            next = strchr(entry, ',');
            if (next) *next++ = '\0';
            colon = strchr(entry, ':');
            if (colon) {
                *colon = '\0';
                key = s_trim(entry);
                val = s_trim(colon + 1);
                /* JS used split(':')[1] - stop at a second colon */
                colon = strchr(val, ':');
                if (colon) {
                    *colon = '\0';
                    s_rtrim(val);
                }
                brick_field(b, key, val);
            }
            entry = next;
        }
        l2_classify(b, ctx->opts);
    }
    /* "firmware revision" and unknown rows are ignored */
}

int l2_parse_brick(const char *txt, const L2ParseOpts *opts, L2Brick *out, int max)
{
    BrickCtx ctx;
    ctx.out = out;
    ctx.max = max;
    ctx.n = 0;
    ctx.cur = NULL;
    ctx.opts = opts;
    for_each_line(txt, brick_line, &ctx);
    return ctx.n;
}

/* ------------------------------------------------------------------ */
/* merge + sort                                                        */
/* ------------------------------------------------------------------ */

void l2_merge_display(L2Brick *bricks, int nb, const L2Display *disp, int nd)
{
    int i, j;
    for (i = 0; i < nb; i++) {
        bricks[i].hasDisplay = 0;
        memset(&bricks[i].display, 0, sizeof(L2Display));
        s_copy(bricks[i].display.id, bricks[i].id, sizeof(bricks[i].display.id));
        for (j = 0; j < nd; j++) {
            if (strcmp(bricks[i].id, disp[j].id) == 0) {
                bricks[i].display = disp[j];
                bricks[i].hasDisplay = 1;
                break;
            }
        }
    }
}

/* parseInt(id.replace(/\D/g, '')) */
static long id_number(const char *id)
{
    long v = 0;
    for (; *id; id++)
        if (isdigit((unsigned char)*id)) v = v * 10 + (*id - '0');
    return v;
}

/* index of id in a whitespace / comma separated list, -1 if missing */
static int order_index(const char *order, const char *id)
{
    const char *p = order, *start;
    size_t len = strlen(id);
    int idx = 0;
    if (!order) return -1;
    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;
        start = p;
        while (*p && !isspace((unsigned char)*p) && *p != ',') p++;
        if ((size_t)(p - start) == len && strncmp(start, id, len) == 0) return idx;
        idx++;
    }
    return -1;
}

/* stable insertion sort - rack sizes are tiny */
static void sort_bricks(L2Brick *b, int n, int (*cmp)(const L2Brick *, const L2Brick *))
{
    int i, j;
    L2Brick tmp;
    for (i = 1; i < n; i++) {
        tmp = b[i];
        j = i - 1;
        while (j >= 0 && cmp(&b[j], &tmp) > 0) {
            b[j + 1] = b[j];
            j--;
        }
        b[j + 1] = tmp;
    }
}

static int cmp_asc(const L2Brick *a, const L2Brick *b)
{
    long x = id_number(a->id), y = id_number(b->id);
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int cmp_desc(const L2Brick *a, const L2Brick *b)
{
    return cmp_asc(b, a);
}

static int cmp_sortnr(const L2Brick *a, const L2Brick *b)
{
    return a->sortNr - b->sortNr;
}

void l2_sort(L2Brick *bricks, int nb, int mode, const char *order)
{
    int i, idx, notInList = 512;
    if (mode == L2_SORT_CUSTOM) {
        for (i = 0; i < nb; i++) {
            idx = order_index(order, bricks[i].id);
            bricks[i].sortNr = idx >= 0 ? idx : notInList++;
        }
        sort_bricks(bricks, nb, cmp_sortnr);
    } else if (mode == L2_SORT_ASC) {
        sort_bricks(bricks, nb, cmp_asc);
    } else {
        sort_bricks(bricks, nb, cmp_desc);
    }
}
