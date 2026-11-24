/*
 * l2parse.h - parsers for SGI L2 controller command output
 *
 * Direct port of parse_config / parse_display / parse_brick and the
 * rack summary sorting from SGI-L2-WUI backend/functions/telnet-api.js
 */
#ifndef SGIL2_L2PARSE_H
#define SGIL2_L2PARSE_H

#define L2_MAX_BRICKS  128
#define L2_MAX_CONFIG  128

/* brick skins (one image + overlay layout each) */
enum {
    SKIN_O300 = 0,
    SKIN_O350,          /* Origin 350 / Onyx 350 */
    SKIN_TEZRO,
    SKIN_A350_YELLOW,
    SKIN_A350_GREY,
    SKIN_A3000,
    SKIN_A3000IX,
    SKIN_NL4R,
    SKIN_ROUTER,
    SKIN_ROUTER_ALTIX,
    SKIN_COUNT
};

/* rack sort modes (config rackSorting) */
enum {
    L2_SORT_DESC = 0,   /* sortAscending: false (default) */
    L2_SORT_ASC,        /* sortAscending: true */
    L2_SORT_CUSTOM      /* useCustomSortOrder: true */
};

typedef struct {
    char id[16];
    char revision[16];
    char line1[40];
    char line2[40];
    int  pwr;
    int  service;       /* attention LED */
    int  fault;         /* failure LED */
} L2Display;

typedef struct {
    char id[16];
    char rack[8];
    char slot[8];
    char partition[16];
    char type[64];
    char serial[24];
    char source[24];
    char brickType;             /* 4th char of id, upper case (C, R, I, X...) */
    int  skin;                  /* SKIN_* */
    char productLabel[48];
    int  hasDisplay;
    L2Display display;
    int  sortNr;
} L2Brick;

typedef struct {
    char ltype[4];              /* "L1" or "L2" */
    char addr[64];
    char id[24];
    char raw[128];
} L2ConfigEntry;

typedef struct {
    int routerAltix;            /* config routerStyle === "altix" */
    int altix350Grey;           /* config altix350Style === "grey" */
} L2ParseOpts;

int  l2_parse_config(const char *txt, L2ConfigEntry *out, int max);
int  l2_parse_display(const char *txt, L2Display *out, int max);
int  l2_parse_brick(const char *txt, const L2ParseOpts *opts, L2Brick *out, int max);

/* attach display data to bricks by id (missing display -> LEDs off) */
void l2_merge_display(L2Brick *bricks, int nb, const L2Display *disp, int nd);

/* sort like cmd_racksummary; order = whitespace/comma separated ids */
void l2_sort(L2Brick *bricks, int nb, int mode, const char *order);

/* classify a brick (sets skin + productLabel); exported for tests */
void l2_classify(L2Brick *b, const L2ParseOpts *opts);

const char *l2_skin_name(int skin);

#endif
